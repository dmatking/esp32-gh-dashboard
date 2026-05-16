# esp32-gh-dashboard

A GitHub traffic dashboard for ESP32-family display boards. Shows views, clones, stars, and day-over-day deltas for all your repositories, cycling through a summary screen and a per-repo detail screen for each.

**Supported boards:**

- **Waveshare ESP32-P4-WIFI6-Touch-LCD-4B** (720×720 round) — flagship target
- **ESP32 "CYD" 2.8" ILI9341** (320×240 — also sold as E32R28T / E32N28T) — compact alternative

<img src="assets/screenshot.png" alt="P4 summary screen" width="240"><img src="assets/screenshot2.png" alt="P4 repo screen" width="240">
<img src="assets/cyd_summary.png" alt="CYD summary screen" width="240"><img src="assets/cyd_repo.png" alt="CYD repo screen" width="240">

---

## What you need

- One of the supported boards above
- A GitHub classic personal access token with `repo` scope — [how to create one](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/managing-your-personal-access-tokens#creating-a-personal-access-token-classic)
- The [github-traffic-log](https://github.com/dmatking/github-traffic-log) companion repo set up and running (collects your traffic data daily)

---

## 1. Flash the firmware

Pick the binary matching your board:

| Board | Binary |
| ----- | ------ |
| Waveshare ESP32-P4 | [`releases/esp32-gh-dashboard-p4-flash.bin`](releases/esp32-gh-dashboard-p4-flash.bin) |
| CYD ESP32 ILI9341 | [`releases/esp32-gh-dashboard-cyd28-flash.bin`](releases/esp32-gh-dashboard-cyd28-flash.bin) |

### Option A — Web flasher (easiest, no install required)

Open **[ESPConnect](https://thelastoutpostworkshop.github.io/ESPConnect/)** in Chrome or Edge, connect your board via USB, and select the binary for your board from the table above.

### Option B — esptool (Python)

For the Waveshare P4:
```bash
pip install esptool
python -m esptool --chip esp32p4 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 releases/esp32-gh-dashboard-p4-flash.bin
```

For the CYD (ESP32):
```bash
pip install esptool
python -m esptool --chip esp32 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 releases/esp32-gh-dashboard-cyd28-flash.bin
```

---

## 2. First-boot setup

On first boot, the device creates a WiFi access point called **GithubDashboard**.

1. Connect your phone or laptop to **GithubDashboard**
2. A setup page should open automatically — if it doesn't, open a browser and go to **http://192.168.4.1/**
3. Fill in the form:
   - **WiFi Network** and **Password**
   - **GitHub Username** and **GitHub Token**
   - **Timezone** — pick from the dropdown, or enter a custom POSIX string
   - **Daily refresh hour** — what hour (0–23) to fetch fresh data each day (default: 6)
   - **Screen cycle time** — seconds between screens (default: 30)
4. Tap **Save & Connect**

The device saves your settings and connects to your WiFi. Subsequent boots connect directly.

**To change any setting**:

- **Waveshare P4**: hold the **BOOT button for 3 seconds** while the device is starting up. The setup page reopens with your existing values pre-filled — change only what you need and tap Save & Connect again.
- **CYD**: re-flash the binary to clear settings (no button-triggered re-provisioning yet). Or, if you've entered a bad GitHub token, the device will detect the auth failure on its next fetch and drop you back into the setup page automatically — see the "GitHub Auth Failed" screen.

---

## 3. Optional: filter which repos appear

Copy [`repo_config.csv`](repo_config.csv) to the root of your `github-traffic-log` repo and edit it to match your repositories:

| repo | show | exclude_totals | effect |
| ---- | ---- | -------------- | ------ |
| my-main-project | 1 | 0 | shown on display, counted in totals |
| old-experiment | 0 | 0 | hidden from display, counted in totals |
| profile-readme | 0 | 1 | hidden from display, excluded from totals and leaderboard |

If the file is absent, all repos are shown and nothing is excluded.

---

For build instructions, project architecture, and hardware details see [BUILDING.md](BUILDING.md).
