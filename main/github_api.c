// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// GitHub API client — fetches pinned repos via GraphQL, traffic via REST.

#include "github_api.h"

#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "github_api";

#define GH_API_BASE    "https://api.github.com"
#define GH_GRAPHQL_URL "https://api.github.com/graphql"
#define BUF_SIZE       16384

// HTTP response accumulation buffer
typedef struct {
    char *data;
    int   len;
    int   cap;
} http_buf_t;

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

static void set_common_headers(esp_http_client_handle_t client, const char *auth)
{
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "User-Agent",    "esp32-gh-dashboard");
}

static bool gh_get(const char *url, char *out_buf, int buf_cap)
{
    http_buf_t buf = { .data = out_buf, .len = 0, .cap = buf_cap };
    out_buf[0] = '\0';

    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_GH_TOKEN);

    esp_http_client_config_t cfg = {
        .url               = url,
        .event_handler     = http_event_cb,
        .user_data         = &buf,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .buffer_size       = 2048,
        .buffer_size_tx    = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    set_common_headers(client, auth);
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GET %s → err=%d status=%d", url, err, status);
        return false;
    }
    return true;
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
    set_common_headers(client, auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
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

bool github_fetch_stats(gh_stats_t *stats, const gh_stats_t *prev)
{
    char *buf = malloc(BUF_SIZE);
    if (!buf) return false;

    memset(stats, 0, sizeof(*stats));

    // 1. Fetch pinned repos via GraphQL
    char gql[512];
    snprintf(gql, sizeof(gql),
        "{\"query\":\"{user(login:\\\"%s\\\"){pinnedItems(first:6,types:REPOSITORY)"
        "{nodes{...on Repository{name description stargazerCount forkCount isPrivate}}}}}\"}",
        CONFIG_GH_USERNAME);

    if (!gh_graphql(gql, buf, BUF_SIZE)) {
        free(buf);
        return false;
    }

    cJSON *root  = cJSON_Parse(buf);
    cJSON *nodes = NULL;
    if (root) {
        cJSON *data  = cJSON_GetObjectItemCaseSensitive(root, "data");
        cJSON *user  = data  ? cJSON_GetObjectItemCaseSensitive(data,  "user")        : NULL;
        cJSON *pins  = user  ? cJSON_GetObjectItemCaseSensitive(user,  "pinnedItems") : NULL;
        nodes        = pins  ? cJSON_GetObjectItemCaseSensitive(pins,  "nodes")       : NULL;
    }

    if (!nodes || !cJSON_IsArray(nodes)) {
        ESP_LOGE(TAG, "Failed to parse pinned repos: %.200s", buf);
        cJSON_Delete(root);
        free(buf);
        return false;
    }

    int count = 0;
    char url[128];
    cJSON *node;
    cJSON_ArrayForEach(node, nodes) {
        if (count >= GH_MAX_REPOS) break;

        gh_repo_t *r = &stats->repos[count];
        json_str(node, "name",        r->name, GH_REPO_NAME_LEN);
        json_str(node, "description", r->desc, GH_DESC_LEN);
        r->stars      = json_int(node, "stargazerCount");
        r->forks      = json_int(node, "forkCount");
        r->is_private = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(node, "isPrivate"));

        // 2. Fetch traffic/views
        snprintf(url, sizeof(url), GH_API_BASE "/repos/%s/%s/traffic/views",
                 CONFIG_GH_USERNAME, r->name);
        if (gh_get(url, buf, BUF_SIZE)) {
            cJSON *tv = cJSON_Parse(buf);
            if (tv) {
                r->views        = json_int(tv, "count");
                r->view_uniques = json_int(tv, "uniques");
                cJSON_Delete(tv);
            }
        }

        // 3. Fetch traffic/clones
        snprintf(url, sizeof(url), GH_API_BASE "/repos/%s/%s/traffic/clones",
                 CONFIG_GH_USERNAME, r->name);
        if (gh_get(url, buf, BUF_SIZE)) {
            cJSON *tc = cJSON_Parse(buf);
            if (tc) {
                r->clones        = json_int(tc, "count");
                r->clone_uniques = json_int(tc, "uniques");
                cJSON_Delete(tc);
            }
        }

        // 4. Compare against previous fetch
        if (prev) {
            for (int i = 0; i < prev->count; i++) {
                if (strcmp(prev->repos[i].name, r->name) == 0) {
                    r->views_changed  = (r->views  > prev->repos[i].views);
                    r->clones_changed = (r->clones > prev->repos[i].clones);
                    break;
                }
            }
        }

        stats->total_views         += r->views;
        stats->total_view_uniques  += r->view_uniques;
        stats->total_clones        += r->clones;
        stats->total_clone_uniques += r->clone_uniques;

        count++;
    }

    stats->count = count;
    cJSON_Delete(root);
    free(buf);

    ESP_LOGI(TAG, "Fetched %d pinned repos, %lu total views, %lu total clones",
             count, (unsigned long)stats->total_views,
             (unsigned long)stats->total_clones);
    return true;
}
