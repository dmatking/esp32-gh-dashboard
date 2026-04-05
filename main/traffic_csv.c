// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT

#include "traffic_csv.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "traffic_csv";

#define CSV_BUF_SIZE 8192

// URL: raw latest.csv from github-traffic-log repo
#define CSV_URL "https://raw.githubusercontent.com/" \
                CONFIG_GH_USERNAME "/github-traffic-log/main/latest.csv"

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

// Parse one CSV data line (non-header) into row.  Returns false if malformed.
static bool parse_line(char *line, csv_row_t *row)
{
    // date,repo,views,view_uniques,clones,clone_uniques,stars,forks
    char *f[8];
    int n = 0;
    char *p = line;
    while (n < 8) {
        f[n++] = p;
        char *comma = strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }
    if (n < 8) return false;

    strncpy(row->date,  f[0], sizeof(row->date)  - 1);
    strncpy(row->repo,  f[1], sizeof(row->repo)  - 1);
    row->date[sizeof(row->date) - 1] = '\0';
    row->repo[sizeof(row->repo) - 1] = '\0';
    row->views        = (uint32_t)atoi(f[2]);
    row->view_uniques = (uint32_t)atoi(f[3]);
    row->clones       = (uint32_t)atoi(f[4]);
    row->clone_uniques = (uint32_t)atoi(f[5]);
    row->stars        = atoi(f[6]);
    row->forks        = atoi(f[7]);
    return true;
}

bool traffic_csv_fetch(csv_data_t *out)
{
    char *buf = malloc(CSV_BUF_SIZE);
    if (!buf) return false;

    memset(out, 0, sizeof(*out));
    http_buf_t hbuf = { .data = buf, .len = 0, .cap = CSV_BUF_SIZE };
    buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url               = CSV_URL,
        .event_handler     = http_event_cb,
        .user_data         = &hbuf,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .buffer_size       = 2048,
        .buffer_size_tx    = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", "esp32-gh-dashboard");

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "CSV fetch failed: err=%d status=%d", err, status);
        free(buf);
        return false;
    }

    // Parse line by line
    char *line = buf;
    bool header = true;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        // Strip trailing \r
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        if (header) {
            header = false;  // skip header row
        } else if (line[0] && out->count < CSV_MAX_ROWS) {
            csv_row_t row = {0};
            if (parse_line(line, &row)) {
                out->rows[out->count++] = row;

                // Track the two most recent distinct dates
                if (out->newest_date[0] == '\0') {
                    strncpy(out->newest_date, row.date, sizeof(out->newest_date) - 1);
                } else if (strcmp(row.date, out->newest_date) > 0) {
                    strncpy(out->prev_date,   out->newest_date, sizeof(out->prev_date) - 1);
                    strncpy(out->newest_date, row.date,         sizeof(out->newest_date) - 1);
                } else if (strcmp(row.date, out->newest_date) < 0) {
                    if (out->prev_date[0] == '\0' ||
                        strcmp(row.date, out->prev_date) > 0) {
                        strncpy(out->prev_date, row.date, sizeof(out->prev_date) - 1);
                    }
                }
            }
        }

        if (!nl) break;
        line = nl + 1;
    }

    free(buf);
    ESP_LOGI(TAG, "Parsed %d rows — newest=%s prev=%s",
             out->count, out->newest_date,
             out->prev_date[0] ? out->prev_date : "(none)");
    return out->count > 0;
}

const csv_row_t *traffic_csv_find(const csv_data_t *data,
                                  const char *date, const char *repo)
{
    for (int i = 0; i < data->count; i++) {
        if (strcmp(data->rows[i].date, date) == 0 &&
            strcmp(data->rows[i].repo, repo) == 0) {
            return &data->rows[i];
        }
    }
    return NULL;
}
