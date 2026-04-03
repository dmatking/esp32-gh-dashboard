// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// GitHub REST API client — fetches repo list and traffic stats.

#include "github_api.h"

#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "github_api";

#define GH_API_BASE  "https://api.github.com"
#define BUF_SIZE     65536

// HTTP response accumulation buffer
typedef struct {
    char  *data;
    int    len;
    int    cap;
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

static bool gh_get(const char *url, char *out_buf, int buf_cap)
{
    http_buf_t buf = { .data = out_buf, .len = 0, .cap = buf_cap };
    out_buf[0] = '\0';

    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_GH_TOKEN);

    esp_http_client_config_t cfg = {
        .url                  = url,
        .event_handler        = http_event_cb,
        .user_data            = &buf,
        .transport_type       = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach    = esp_crt_bundle_attach,
        .timeout_ms           = 10000,
        .buffer_size          = 2048,
        .buffer_size_tx       = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "User-Agent",    "esp32-gh-dashboard");
    esp_http_client_set_header(client, "Accept",        "application/vnd.github+json");
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

    // 1. Fetch repo list
    char url[128];
    snprintf(url, sizeof(url),
             GH_API_BASE "/user/repos?per_page=100&type=owner&sort=updated");

    if (!gh_get(url, buf, BUF_SIZE)) {
        free(buf);
        return false;
    }

    cJSON *repos_json = cJSON_Parse(buf);
    if (!repos_json || !cJSON_IsArray(repos_json)) {
        ESP_LOGE(TAG, "Failed to parse repo list (len=%d): %.200s", strlen(buf), buf);
        cJSON_Delete(repos_json);
        free(buf);
        return false;
    }

    int count = 0;
    cJSON *repo_item;
    cJSON_ArrayForEach(repo_item, repos_json) {
        if (count >= GH_MAX_REPOS) break;

        gh_repo_t *r = &stats->repos[count];
        json_str(repo_item, "name", r->name, GH_REPO_NAME_LEN);
        json_str(repo_item, "description", r->desc, GH_DESC_LEN);
        r->stars      = json_int(repo_item, "stargazers_count");
        r->forks      = json_int(repo_item, "forks_count");
        r->is_private = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(repo_item, "private"));

        // 2. Fetch traffic/views
        snprintf(url, sizeof(url),
                 GH_API_BASE "/repos/%s/%s/traffic/views",
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
        snprintf(url, sizeof(url),
                 GH_API_BASE "/repos/%s/%s/traffic/clones",
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

        stats->total_views        += r->views;
        stats->total_view_uniques += r->view_uniques;
        stats->total_clones       += r->clones;
        stats->total_clone_uniques+= r->clone_uniques;

        count++;
    }

    stats->count = count;
    cJSON_Delete(repos_json);
    free(buf);

    ESP_LOGI(TAG, "Fetched %d repos, %lu total views, %lu total clones",
             count, (unsigned long)stats->total_views,
             (unsigned long)stats->total_clones);
    return true;
}
