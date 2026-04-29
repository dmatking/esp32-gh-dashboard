// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// Fetches latest.csv (daily counts, last 2 dates) and totals.csv
// (cumulative all-time sums) from the github-traffic-log repo.

#include "traffic_csv.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "traffic_csv";

#define CSV_BUF_SIZE    8192
#define TOTALS_BUF_SIZE 4096

#define CSV_URL    "https://raw.githubusercontent.com/" \
                   CONFIG_GH_USERNAME "/github-traffic-log/main/latest.csv"
#define TOTALS_URL "https://raw.githubusercontent.com/" \
                   CONFIG_GH_USERNAME "/github-traffic-log/main/totals.csv"

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

// ---------------------------------------------------------------------------
// totals.csv — cumulative all-time sums, one row per repo
// Format: repo,views,view_uniques,clones,clone_uniques,stars,forks
// ---------------------------------------------------------------------------

static bool parse_totals_line(char *line, totals_row_t *row)
{
    char *f[7];
    int n = 0;
    char *p = line;
    while (n < 7) {
        f[n++] = p;
        char *comma = strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }
    if (n < 7) return false;

    strncpy(row->repo, f[0], sizeof(row->repo) - 1);
    row->repo[sizeof(row->repo) - 1] = '\0';
    row->views         = (uint32_t)atoi(f[1]);
    row->view_uniques  = (uint32_t)atoi(f[2]);
    row->clones        = (uint32_t)atoi(f[3]);
    row->clone_uniques = (uint32_t)atoi(f[4]);
    row->stars         = atoi(f[5]);
    row->forks         = atoi(f[6]);
    return true;
}

bool traffic_totals_fetch(totals_data_t *out)
{
    char *buf = malloc(TOTALS_BUF_SIZE);
    if (!buf) return false;

    memset(out, 0, sizeof(*out));
    http_buf_t hbuf = { .data = buf, .len = 0, .cap = TOTALS_BUF_SIZE };
    buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url               = TOTALS_URL,
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
        ESP_LOGW(TAG, "totals.csv fetch failed: err=%d status=%d", err, status);
        free(buf);
        return false;
    }

    char *line = buf;
    bool header = true;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        if (header) {
            header = false;
        } else if (line[0] && out->count < GH_MAX_REPOS) {
            totals_row_t row = {0};
            if (parse_totals_line(line, &row))
                out->rows[out->count++] = row;
        }

        if (!nl) break;
        line = nl + 1;
    }

    free(buf);
    ESP_LOGI(TAG, "Parsed %d totals rows", out->count);
    return out->count > 0;
}

const totals_row_t *traffic_totals_find(const totals_data_t *data,
                                        const char *repo)
{
    for (int i = 0; i < data->count; i++) {
        if (strcmp(data->rows[i].repo, repo) == 0)
            return &data->rows[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// traffic.csv — full daily history, one row per (date, repo)
// ---------------------------------------------------------------------------

#define HISTORY_URL      "https://raw.githubusercontent.com/" \
                         CONFIG_GH_USERNAME "/github-traffic-log/main/traffic.csv"
#define HISTORY_BUF_SIZE 32768
#define MAX_HIST_DATES   64   // scratch for date collection before truncating to 14

static int cmp_date_desc(const void *a, const void *b)
{
    return strcmp((const char *)b, (const char *)a);
}

bool traffic_history_fetch(gh_stats_t *stats)
{
    char *buf = malloc(HISTORY_BUF_SIZE);
    if (!buf) return false;

    http_buf_t hbuf = { .data = buf, .len = 0, .cap = HISTORY_BUF_SIZE };
    buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url               = HISTORY_URL,
        .event_handler     = http_event_cb,
        .user_data         = &hbuf,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 15000,
        .buffer_size       = 4096,
        .buffer_size_tx    = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", "esp32-gh-dashboard");

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "traffic.csv fetch failed: err=%d status=%d", err, status);
        free(buf);
        return false;
    }

    // Pass 1 (read-only): collect all unique dates seen in the file.
    char all_dates[MAX_HIST_DATES][12];
    int  ndate = 0;
    {
        const char *p = buf;
        bool hdr = true;
        while (*p) {
            const char *nl  = strchr(p, '\n');
            const char *eol = nl ? nl : p + strlen(p);
            if (!hdr) {
                const char *comma = (const char *)memchr(p, ',', eol - p);
                if (comma && (comma - p) == 10) {
                    char d[11];
                    memcpy(d, p, 10);
                    d[10] = '\0';
                    bool found = false;
                    for (int i = 0; i < ndate; i++)
                        if (strncmp(all_dates[i], d, 10) == 0) { found = true; break; }
                    if (!found && ndate < MAX_HIST_DATES)
                        memcpy(all_dates[ndate++], d, 11);
                }
            }
            hdr = false;
            if (!nl) break;
            p = nl + 1;
        }
    }

    if (ndate == 0) {
        free(buf);
        return false;
    }

    // Sort descending (newest first), keep at most HISTORY_DAYS.
    qsort(all_dates, ndate, sizeof(all_dates[0]), cmp_date_desc);
    int nslots = (ndate < HISTORY_DAYS) ? ndate : HISTORY_DAYS;
    // Slot mapping: all_dates[0]=newest → slot (nslots-1),  all_dates[nslots-1]=oldest → slot 0

    // Pass 2 (modifies buf): parse rows and fill history arrays.
    char *line = buf;
    bool header = true;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        if (!header && line[0]) {
            csv_row_t row = {0};
            if (parse_line(line, &row)) {
                // Map date → slot index
                int slot = -1;
                for (int d = 0; d < nslots; d++) {
                    if (strcmp(all_dates[d], row.date) == 0) {
                        slot = (nslots - 1) - d;
                        break;
                    }
                }
                if (slot >= 0) {
                    for (int i = 0; i < stats->count; i++) {
                        if (strcmp(stats->repos[i].name, row.repo) == 0) {
                            stats->repos[i].history_views [slot] += row.views;
                            stats->repos[i].history_clones[slot] += row.clones;
                            stats->history_total_views [slot] += row.views;
                            stats->history_total_clones[slot] += row.clones;
                            break;
                        }
                    }
                }
            }
        }
        header = false;
        if (!nl) break;
        line = nl + 1;
    }

    free(buf);
    ESP_LOGI(TAG, "history: %d dates, %d slots filled", ndate, nslots);
    return true;
}
