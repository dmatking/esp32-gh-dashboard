// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// GitHub stats dashboard for Waveshare ESP32-P4 720x720

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "board_interface.h"
#include "wifi.h"
#include "github_api.h"
#include "dashboard.h"

#define NVS_NAMESPACE "dashboard"
#define NVS_STATS_KEY "gh_stats"

static void stats_save(const gh_stats_t *stats)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_STATS_KEY, stats, sizeof(*stats));
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI("dashboard", "Stats saved to NVS");
}

static bool stats_load(gh_stats_t *stats)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = sizeof(*stats);
    esp_err_t err = nvs_get_blob(h, NVS_STATS_KEY, stats, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(*stats)) return false;
    ESP_LOGI("dashboard", "Previous stats loaded from NVS (%d repos)", stats->count);
    return true;
}

static const char *TAG = "dashboard";

#define CYCLE_MS  (CONFIG_DASHBOARD_CYCLE_SEC * 1000)

#define REFRESH_HOUR CONFIG_DASHBOARD_REFRESH_HOUR

static void sync_time(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);

    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    esp_netif_sntp_deinit();

    if (err == ESP_OK) {
        setenv("TZ", CONFIG_DASHBOARD_TIMEZONE, 1);
        tzset();
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d Central",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    } else {
        ESP_LOGW(TAG, "NTP sync failed (err=0x%x) — scheduled refresh disabled", err);
    }
}

// Returns true if clock is valid (post-2024) and past REFRESH_HOUR today,
// and we haven't already fetched on this calendar day.
static bool should_refresh(int *last_fetch_yday)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    if (t.tm_year + 1900 < 2024) return false;  // clock not synced
    if (t.tm_hour < REFRESH_HOUR)  return false;  // too early
    if (t.tm_yday == *last_fetch_yday) return false;  // already fetched today

    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main start");

    board_init();

    dashboard_draw_fetching();

    if (!wifi_connect()) {
        dashboard_draw_error("WiFi failed");
        ESP_LOGE(TAG, "WiFi connect failed — halting");
        vTaskDelay(portMAX_DELAY);
    }

    sync_time();

    static gh_stats_t stats;
    static gh_stats_t prev_stats;
    bool have_prev = stats_load(&prev_stats);

    dashboard_draw_fetching();
    ESP_LOGI(TAG, "Fetching GitHub stats...");
    if (!github_fetch_stats(&stats, have_prev ? &prev_stats : NULL)) {
        dashboard_draw_error("GitHub API failed");
        ESP_LOGE(TAG, "Initial fetch failed");
        vTaskDelay(portMAX_DELAY);
    }
    ESP_LOGI(TAG, "Fetched %d repos", stats.count);
    stats_save(&stats);

    // Record which day we last fetched so we don't re-fetch until tomorrow
    int last_fetch_yday = -1;
    {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        if (t.tm_year + 1900 >= 2024) last_fetch_yday = t.tm_yday;
    }

    int screen_idx = -1;

    while (1) {
        if (should_refresh(&last_fetch_yday)) {
            ESP_LOGI(TAG, "Scheduled 6 AM refresh...");
            memcpy(&prev_stats, &stats, sizeof(stats));
            if (github_fetch_stats(&stats, &prev_stats)) {
                stats_save(&stats);
                time_t now = time(NULL);
                struct tm t;
                localtime_r(&now, &t);
                last_fetch_yday = t.tm_yday;
            }
        }

        if (screen_idx < 0) {
            dashboard_draw_summary(&stats);
        } else {
            dashboard_draw_repo(&stats, screen_idx);
        }

        vTaskDelay(pdMS_TO_TICKS(CYCLE_MS));

        screen_idx++;
        if (screen_idx >= stats.count) screen_idx = -1;
    }
}
