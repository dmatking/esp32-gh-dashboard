// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// GitHub API client — fetches all user repos via GraphQL, traffic via CSV.

#include "github_api.h"
#include "traffic_csv.h"

#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "github_api";

// Replace common multi-byte UTF-8 sequences with ASCII equivalents in-place.
static void sanitize_desc(char *s)
{
    char out[GH_DESC_LEN];
    int si = 0, oi = 0;
    int slen = strlen(s);
    while (si < slen && oi < (int)sizeof(out) - 1) {
        unsigned char c = (unsigned char)s[si];
        if (c < 0x80) {
            out[oi++] = s[si++];
        } else if (c == 0xE2 && si + 2 < slen) {
            unsigned char b1 = (unsigned char)s[si+1];
            unsigned char b2 = (unsigned char)s[si+2];
            if (b1 == 0x80) {
                if (b2 == 0x93) { out[oi++] = '-'; si += 3; continue; }
                if (b2 == 0x94) { out[oi++] = '-'; si += 3; continue; }
                if (b2 == 0x9C || b2 == 0x9D) { out[oi++] = '"'; si += 3; continue; }
                if (b2 == 0x98 || b2 == 0x99) { out[oi++] = '\''; si += 3; continue; }
                if (b2 == 0xA6) {
                    if (oi + 3 < (int)sizeof(out) - 1) { out[oi++]='.'; out[oi++]='.'; out[oi++]='.'; }
                    si += 3; continue;
                }
            }
            si++;
        } else {
            si++;
        }
    }
    out[oi] = '\0';
    memcpy(s, out, oi + 1);
}

#define GH_GRAPHQL_URL "https://api.github.com/graphql"
#define BUF_SIZE       12288

typedef struct { char *data; int len; int cap; } http_buf_t;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        if (buf->len + evt->data_len < buf->cap - 1) {
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            buf->data[buf->len] = '\0';
        }
    }
    return ESP_OK;
}

static bool gh_graphql(const char *query_body, char *out_buf, int buf_cap)
{
    http_buf_t buf = { .data = out_buf, .len = 0, .cap = buf_cap };
    out_buf[0] = '\0';

    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_GH_TOKEN);

    esp_http_client_config_t cfg = {
        .url               = GH_GRAPHQL_URL,
        .method            = HTTP_METHOD_POST,
        .event_handler     = http_event_cb,
        .user_data         = &buf,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .buffer_size       = 2048,
        .buffer_size_tx    = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "User-Agent",    "esp32-gh-dashboard");
    esp_http_client_set_header(client, "Content-Type",  "application/json");
    esp_http_client_set_post_field(client, query_body, strlen(query_body));

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GraphQL → err=%d status=%d", err, status);
        return false;
    }
    return true;
}

static int json_int(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (item && cJSON_IsNumber(item)) ? (int)item->valuedouble : 0;
}

static void json_str(cJSON *obj, const char *key, char *dst, int dst_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dst_len - 1);
        dst[dst_len - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

bool github_fetch_stats(gh_stats_t *stats)
{
    char *buf = malloc(BUF_SIZE);
    if (!buf) return false;

    memset(stats, 0, sizeof(*stats));

    // 1. Fetch all user repos via GraphQL (names, descriptions, stars, forks)
    char gql[512];
    snprintf(gql, sizeof(gql),
        "{\"query\":\"{user(login:\\\"%s\\\"){repositories(first:%d,ownerAffiliations:OWNER,"
        "orderBy:{field:PUSHED_AT,direction:DESC})"
        "{nodes{name description stargazerCount forkCount isPrivate}}}}\"}",
        CONFIG_GH_USERNAME, GH_MAX_REPOS);

    if (!gh_graphql(gql, buf, BUF_SIZE)) {
        free(buf);
        return false;
    }

    cJSON *root  = cJSON_Parse(buf);
    cJSON *nodes = NULL;
    if (root) {
        cJSON *data  = cJSON_GetObjectItemCaseSensitive(root, "data");
        cJSON *user  = data ? cJSON_GetObjectItemCaseSensitive(data, "user")         : NULL;
        cJSON *repos = user ? cJSON_GetObjectItemCaseSensitive(user, "repositories") : NULL;
        nodes        = repos ? cJSON_GetObjectItemCaseSensitive(repos, "nodes")      : NULL;
    }

    if (!nodes || !cJSON_IsArray(nodes)) {
        ESP_LOGE(TAG, "Failed to parse repos: %.200s", buf);
        cJSON_Delete(root);
        free(buf);
        return false;
    }

    // Collect repo names and metadata
    int count = 0;
    cJSON *node;
    cJSON_ArrayForEach(node, nodes) {
        if (count >= GH_MAX_REPOS) break;
        gh_repo_t *r = &stats->repos[count];
        json_str(node, "name",        r->name, GH_REPO_NAME_LEN);
        json_str(node, "description", r->desc, GH_DESC_LEN);
        sanitize_desc(r->desc);
        r->stars      = json_int(node, "stargazerCount");
        r->forks      = json_int(node, "forkCount");
        r->is_private = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(node, "isPrivate"));
        count++;
    }
    stats->count = count;
    cJSON_Delete(root);
    free(buf);

    // 2. Fetch traffic CSV (one request for all repos)
    static csv_data_t csv;   // static to avoid stack pressure
    if (!traffic_csv_fetch(&csv)) {
        ESP_LOGW(TAG, "CSV fetch failed — traffic data unavailable");
        return true;  // partial success: metadata ok, traffic zeroed
    }

    // 3. Populate each repo from CSV and compute day-over-day deltas
    for (int i = 0; i < stats->count; i++) {
        gh_repo_t *r = &stats->repos[i];

        const csv_row_t *today = traffic_csv_find(&csv, csv.newest_date, r->name);
        const csv_row_t *prev  = csv.prev_date[0]
                                 ? traffic_csv_find(&csv, csv.prev_date, r->name)
                                 : NULL;

        if (today) {
            r->views        = today->views;
            r->view_uniques = today->view_uniques;
            r->clones       = today->clones;
            r->clone_uniques = today->clone_uniques;
            // stars/forks from CSV may be more current than GraphQL cache
            if (today->stars > r->stars) r->stars = today->stars;
            if (today->forks > r->forks) r->forks = today->forks;
        }

        if (today && prev) {
            r->views_delta        = (today->views        > prev->views)        ? today->views        - prev->views        : 0;
            r->view_uniques_delta = (today->view_uniques > prev->view_uniques) ? today->view_uniques - prev->view_uniques : 0;
            r->clones_delta       = (today->clones       > prev->clones)       ? today->clones       - prev->clones       : 0;
            r->clone_uniques_delta = (today->clone_uniques > prev->clone_uniques) ? today->clone_uniques - prev->clone_uniques : 0;
            r->views_changed  = (r->views_delta  > 0);
            r->clones_changed = (r->clones_delta > 0);
        }

        stats->total_views               += r->views;
        stats->total_view_uniques        += r->view_uniques;
        stats->total_clones              += r->clones;
        stats->total_clone_uniques       += r->clone_uniques;
        stats->total_views_delta         += r->views_delta;
        stats->total_view_uniques_delta  += r->view_uniques_delta;
        stats->total_clones_delta        += r->clones_delta;
        stats->total_clone_uniques_delta += r->clone_uniques_delta;
    }

    ESP_LOGI(TAG, "Fetched %d repos — newest=%s prev=%s",
             count, csv.newest_date, csv.prev_date[0] ? csv.prev_date : "(none)");
    return true;
}
