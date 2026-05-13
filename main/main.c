// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// GitHub stats dashboard for Waveshare ESP32-P4 720x720

#include <stdlib.h>
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
#ifndef BOARD_NATIVE_WIFI
#include "esp_hosted.h"
#endif
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

static bool s_prov_failed;

static void connect_failed_display(const char *ap_ssid, void *ctx)
{
    (void)ap_ssid;
    (void)ctx;
    s_prov_failed = true;
}

static void portal_display(const char *ap_ssid, void *ctx)
{
    (void)ctx;
    const char *title = s_prov_failed ? "Wrong Password?" : "WiFi Setup";
    s_prov_failed = false;
    dashboard_draw_provisioning(title, ap_ssid);
}

static void sync_time(const char *tz)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    esp_netif_sntp_deinit();

    if (err == ESP_OK) {
        setenv("TZ", tz, 1);
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

static bool should_refresh(int *last_fetch_yday, int refresh_hour)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year + 1900 < 2024)        return false;
    if (t.tm_hour < refresh_hour)        return false;
    if (t.tm_yday == *last_fetch_yday)   return false;
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main start");

    board_init();
    button_init();

    dashboard_draw_fetching();

#ifndef BOARD_NATIVE_WIFI
    // P4: init C6 co-processor before any WiFi calls
    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_ERROR_CHECK(esp_hosted_connect_to_slave());
#endif

    static const wifi_prov_option_t tz_options[] = {
        { "US Eastern (ET)",            "EST5EDT,M3.2.0,M11.1.0"          },
        { "US Central (CT)",            "CST6CDT,M3.2.0,M11.1.0"          },
        { "US Mountain (MT)",           "MST7MDT,M3.2.0,M11.1.0"          },
        { "US Mountain - Arizona",      "MST7"                             },
        { "US Pacific (PT)",            "PST8PDT,M3.2.0,M11.1.0"          },
        { "US Alaska",                  "AKST9AKDT,M3.2.0,M11.1.0"        },
        { "US Hawaii",                  "HST10"                            },
        { "Canada - Newfoundland",      "NST3:30NDT,M3.2.0,M11.1.0"       },
        { "UK / Ireland",               "GMT0BST,M3.5.0/1,M10.5.0"        },
        { "Central Europe (CET)",       "CET-1CEST,M3.5.0,M10.5.0/3"     },
        { "Eastern Europe (EET)",       "EET-2EEST,M3.5.0/3,M10.5.0/4"   },
        { "Moscow",                     "MSK-3"                            },
        { "India (IST)",                "IST-5:30"                         },
        { "China / Singapore",          "CST-8"                            },
        { "Japan / Korea",              "JST-9"                            },
        { "Australia Eastern (AEST)",   "AEST-10AEDT,M10.1.0,M4.1.0/3"   },
        { "Australia Central (ACST)",   "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
        { "Australia Western (AWST)",   "AWST-8"                           },
        { "New Zealand",                "NZST-12NZDT,M9.5.0,M4.1.0/3"    },
        { "UTC",                        "UTC0"                             },
    };
    static const wifi_prov_field_t prov_fields[] = {
        { .key = "gh_user",
          .label = "GitHub Username",
          .placeholder = "your-github-username" },
        { .key = "gh_token",
          .label = "GitHub Token (classic, repo scope)",
          .placeholder = "ghp_...", .secret = true },
        { .key = "tz",
          .label = "Timezone",
          .options = tz_options,
          .option_count = sizeof(tz_options) / sizeof(tz_options[0]) },
        { .key = "tz_custom",
          .label = "Custom timezone (optional, overrides above)",
          .placeholder = "e.g. EST5EDT,M3.2.0,M11.1.0" },
        { .key = "refresh_hr",
          .label = "Daily refresh hour (0-23)",
          .input_type = "number", .input_min = "0", .input_max = "23",
          .placeholder = "6" },
        { .key = "cycle_sec",
          .label = "Screen cycle time (seconds)",
          .input_type = "number", .input_min = "5", .input_max = "300",
          .placeholder = "30" },
    };
    wifi_prov_config_t prov_cfg = {
        .ap_ssid           = "GithubDashboard",
        .boot_gpio         = BOOT_BUTTON_GPIO,
        .on_portal         = portal_display,
        .on_connect_failed = connect_failed_display,
        .extra_fields      = prov_fields,
        .extra_count       = sizeof(prov_fields) / sizeof(prov_fields[0]),
    };
    if (!wifi_prov_start(&prov_cfg)) {
        dashboard_draw_error("WiFi failed");
        vTaskDelay(portMAX_DELAY);
    }

    // Read credentials from NVS
    char gh_user[64]  = { 0 };
    char gh_token[128] = { 0 };
    wifi_prov_get("gh_user",  gh_user,  sizeof(gh_user));
    wifi_prov_get("gh_token", gh_token, sizeof(gh_token));
    github_set_credentials(gh_user[0]  ? gh_user  : NULL,
                           gh_token[0] ? gh_token : NULL);
    traffic_csv_set_username(gh_user[0] ? gh_user : NULL);

    // Timezone: custom field overrides dropdown selection
    char tz_sel[64]  = { 0 };
    char tz_cust[64] = { 0 };
    wifi_prov_get("tz",        tz_sel,  sizeof(tz_sel));
    wifi_prov_get("tz_custom", tz_cust, sizeof(tz_cust));
    const char *tz = tz_cust[0] ? tz_cust
                   : tz_sel[0]  ? tz_sel
                   : CONFIG_DASHBOARD_TIMEZONE;

    // Refresh hour and cycle time (fall back to Kconfig defaults)
    char rh_str[8] = { 0 }, cs_str[8] = { 0 };
    wifi_prov_get("refresh_hr", rh_str, sizeof(rh_str));
    wifi_prov_get("cycle_sec",  cs_str, sizeof(cs_str));
    int refresh_hour = rh_str[0] ? atoi(rh_str) : CONFIG_DASHBOARD_REFRESH_HOUR;
    int cycle_ms     = (cs_str[0] ? atoi(cs_str) : CONFIG_DASHBOARD_CYCLE_SEC) * 1000;

    sync_time(tz);

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
        if (should_refresh(&last_fetch_yday, refresh_hour)) {
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

        wait_or_button(cycle_ms);

        screen_idx++;
        while (screen_idx < stats.count && stats.repos[screen_idx].hide)
            screen_idx++;
        if (screen_idx >= stats.count) screen_idx = -1;
    }
}
