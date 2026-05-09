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
#include "driver/gpio.h"

#include "board_interface.h"
#include "wifi_prov.h"
#include "esp_hosted.h"
#include "github_api.h"
#include "traffic_csv.h"
#include "dashboard.h"

#define BOOT_BUTTON_GPIO  35   // ESP32-P4 BOOT/STRAPPING pin, active-low

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

// Wait up to timeout_ms, return early if BOOT button pressed.
static bool wait_or_button(int timeout_ms)
{
    const int POLL_MS = 50;
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            while (gpio_get_level(BOOT_BUTTON_GPIO) == 0)
                vTaskDelay(pdMS_TO_TICKS(20));
            return true;
        }
    }
    return false;
}

static const char *TAG = "dashboard";

static void portal_display(const char *ap_ssid, void *ctx)
{
    (void)ctx;
    char msg[96];
    snprintf(msg, sizeof(msg), "Setup mode - connect\nphone to WiFi:\n%s", ap_ssid);
    dashboard_draw_error(msg);
}

#define CYCLE_MS     (CONFIG_DASHBOARD_CYCLE_SEC * 1000)
#define REFRESH_HOUR  CONFIG_DASHBOARD_REFRESH_HOUR

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
        ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    } else {
        ESP_LOGW(TAG, "NTP sync failed — scheduled refresh disabled");
    }
}

static bool should_refresh(int *last_fetch_yday)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year + 1900 < 2024) return false;
    if (t.tm_hour < REFRESH_HOUR)  return false;
    if (t.tm_yday == *last_fetch_yday) return false;
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main start");

    board_init();
    button_init();

    dashboard_draw_fetching();

    // P4: init C6 co-processor before any WiFi calls
    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_ERROR_CHECK(esp_hosted_connect_to_slave());

    static const wifi_prov_field_t prov_fields[] = {
        { .key = "gh_user",  .label = "GitHub Username",
          .placeholder = "your-github-username" },
        { .key = "gh_token", .label = "GitHub Token",
          .placeholder = "ghp_...", .secret = true },
    };
    wifi_prov_config_t prov_cfg = {
        .ap_ssid      = "DashboardSetup",
        .boot_gpio    = BOOT_BUTTON_GPIO,
        .on_portal    = portal_display,
        .extra_fields = prov_fields,
        .extra_count  = 2,
    };
    if (!wifi_prov_start(&prov_cfg)) {
        dashboard_draw_error("WiFi failed");
        vTaskDelay(portMAX_DELAY);
    }

    // Read runtime credentials from NVS and pass to API modules
    char gh_user[64]  = { 0 };
    char gh_token[128] = { 0 };
    wifi_prov_get("gh_user",  gh_user,  sizeof(gh_user));
    wifi_prov_get("gh_token", gh_token, sizeof(gh_token));
    github_set_credentials(gh_user[0]  ? gh_user  : NULL,
                           gh_token[0] ? gh_token : NULL);
    traffic_csv_set_username(gh_user[0] ? gh_user : NULL);

    sync_time();

    static gh_stats_t stats;

    dashboard_draw_fetching();
    if (!github_fetch_stats(&stats)) {
        dashboard_draw_error("Fetch failed");
        vTaskDelay(portMAX_DELAY);
    }

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
            ESP_LOGI(TAG, "Scheduled refresh...");
            if (github_fetch_stats(&stats)) {
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

        wait_or_button(CYCLE_MS);

        screen_idx++;
        while (screen_idx < stats.count && stats.repos[screen_idx].hide)
            screen_idx++;
        if (screen_idx >= stats.count) screen_idx = -1;
    }
}
