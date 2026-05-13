# Building from source

## Requirements

- [ESP-IDF](https://github.com/espressif/esp-idf) v5.5.3 or later
- Target: `esp32p4`

## Build and flash

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash
```

To create a merged single-file binary (suitable for the web flasher):

```bash
idf.py merge-bin -o esp32-gh-dashboard-flash.bin
```

Output is in `build/esp32-gh-dashboard-flash.bin`, ready to flash to offset `0x0`.

## Configuration

Adjust via `idf.py menuconfig` → **Dashboard Configuration**:

| Option                   | Default                    | Description                                |
| ------------------------ | -------------------------- | ------------------------------------------ |
| `DASHBOARD_TIMEZONE`     | `CST6CDT,M3.2.0,M11.1.0`  | POSIX TZ string (US Central w/ DST)        |
| `DASHBOARD_REFRESH_HOUR` | `6`                        | Hour of day to re-fetch stats (local time) |
| `DASHBOARD_CYCLE_SEC`    | `30`                       | Seconds between screen transitions         |
| `GH_USERNAME`            | *(empty)*                  | Compile-time fallback username (optional)  |
| `GH_TOKEN`               | *(empty)*                  | Compile-time fallback classic token (optional) |

`GH_USERNAME` and `GH_TOKEN` are only used if NVS is empty (i.e. provisioning has never run). Normally credentials are entered via the first-boot portal.

Common timezone strings:

```
CST6CDT,M3.2.0,M11.1.0   US Central
EST5EDT,M3.2.0,M11.1.0   US Eastern
MST7MDT,M3.2.0,M11.1.0   US Mountain
PST8PDT,M3.2.0,M11.1.0   US Pacific
UTC0                      UTC
```

## Hardware

| Part    | Details                               |
| ------- | ------------------------------------- |
| Board   | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B |
| Display | 720×720 MIPI-DSI (ST7703 controller)  |
| WiFi    | ESP32-C6 co-processor via SDIO        |
| Flash   | 16 MB                                 |
| PSRAM   | 32 MB HEX mode, 200 MHz               |

> For ESP32-P4 + C6 chip-level notes (esp_hosted, SDIO, PSRAM, errata) see [esp32-notes](https://github.com/dmatking/esp32-notes).

## How it works

Traffic data is collected by the [github-traffic-log](https://github.com/dmatking/github-traffic-log) companion GitHub Action, which runs daily and appends stats to CSV files. The dashboard fetches these on boot and at a configurable daily refresh hour.

- `latest.csv` — last two days of per-repo traffic (views, clones, uniques)
- `totals.csv` — cumulative all-time sums per repo
- `traffic.csv` — full 14-day daily history per repo (used for the graph)
- `repo_config.csv` — optional per-repo display flags (`show`, `exclude_totals`)

Repo names and descriptions come from the GitHub GraphQL API. GitHub's traffic API only retains 14 days — the CSV log provides permanent accumulation beyond that window.

Credentials (WiFi SSID/password, GitHub username and token) are entered via a captive-portal web form on first boot and stored in NVS flash. Subsequent boots skip provisioning and connect directly.

## Project structure

```
main/
  main.c                                App entry point, scheduling, button handling
  board_waveshare_wvshr_p4_720_touch.c  Display init and pixel API
  board_interface.h                     Board abstraction (pixel, flush, dimensions)
  dashboard.c / .h                      Screen renderer
  github_api.c / .h                     GraphQL client for repo metadata
  traffic_csv.c / .h                    CSV fetcher and parser for traffic data
  font8x16.c / .h                       Bitmap font renderer
  Kconfig.projbuild                     menuconfig options
components/
  wifi_prov/                            First-boot captive portal provisioning (NVS)
  nordesems__esp-captive-portal/        DNS hijack + OS probe endpoint handling
  esp_lcd_st7703/                       ST7703 MIPI-DSI panel driver
sdkconfig.defaults                      ESP32-P4 + Waveshare board settings
partitions.csv                          16 MB flash layout (4 MB app partition)
releases/
  esp32-gh-dashboard-flash.bin         Pre-built merged binary (flash to offset 0x0)
```
