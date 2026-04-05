// main_sim.c — desktop sim entry point for esp32-gh-dashboard.
// No WiFi, no NTP, no hardware. Feeds real traffic data to the dashboard
// renderer and cycles through screens.
//
// Interactive: screens auto-advance every 4 seconds.
//              Press P to save a screenshot, Esc/close to quit.
// Headless:    ./gh_dashboard_sim --screenshot summary.png --frames 1
//              ./gh_dashboard_sim --screenshot repo1.png   --frames 2
//              ./gh_dashboard_sim --screenshot repo2.png   --frames 3
//              (frames N = show screen N, 1-indexed: 1=summary, 2=repo[0], ...)

#include <SDL2/SDL.h>
#include <stdio.h>

#include "board_interface.h"
#include "dashboard.h"
#include "github_api.h"
#include "screencap.h"

int   sim_argc;
char **sim_argv;

// ---------------------------------------------------------------------------
// Stats from traffic CSV 2026-04-05 — top 6 repos by combined views+clones.
// views/clones are 14-day rolling totals; *_delta are today's increments.
// ---------------------------------------------------------------------------
static const gh_stats_t STATS = {
    .count = 6,
    .repos = {
        {
            .name             = "esp32-terminal",
            .desc             = "ESP32-P4 SSH terminal with BT keyboard",
            .stars = 1, .forks = 0,
            .views = 45, .view_uniques = 6,
            .clones = 83, .clone_uniques = 46,
            .views_changed = true, .clones_changed = true,
            .views_delta = 45, .view_uniques_delta = 6,
            .clones_delta = 83, .clone_uniques_delta = 46,
        },
        {
            .name             = "esp32-idf-new",
            .desc             = "ESP-IDF new-project scaffolding tool",
            .stars = 0, .forks = 0,
            .views = 59, .view_uniques = 1,
            .clones = 68, .clone_uniques = 31,
            .views_changed = true, .clones_changed = true,
            .views_delta = 59, .view_uniques_delta = 1,
            .clones_delta = 68, .clone_uniques_delta = 31,
        },
        {
            .name             = "esp32-video-stream",
            .desc             = "MJPEG video streaming over WiFi",
            .stars = 0, .forks = 0,
            .views = 25, .view_uniques = 2,
            .clones = 92, .clone_uniques = 64,
            .views_changed = true, .clones_changed = true,
            .views_delta = 25, .view_uniques_delta = 2,
            .clones_delta = 92, .clone_uniques_delta = 64,
        },
        {
            .name             = "esp32-p4-webradio",
            .desc             = "Internet radio on ESP32-P4 with LCD",
            .stars = 0, .forks = 0,
            .views = 30, .view_uniques = 1,
            .clones = 68, .clone_uniques = 33,
            .views_changed = true, .clones_changed = true,
            .views_delta = 30, .view_uniques_delta = 1,
            .clones_delta = 68, .clone_uniques_delta = 33,
        },
        {
            .name             = "esp32-gh-dashboard",
            .desc             = "GitHub stats dashboard on ESP32-P4",
            .stars = 0, .forks = 0,
            .views = 22, .view_uniques = 1,
            .clones = 62, .clone_uniques = 45,
            .views_changed = true, .clones_changed = true,
            .views_delta = 22, .view_uniques_delta = 1,
            .clones_delta = 62, .clone_uniques_delta = 45,
        },
        {
            .name             = "esp32-p4-demos",
            .desc             = "Demo collection for ESP32-P4 devkit",
            .stars = 1, .forks = 0,
            .views = 14, .view_uniques = 4,
            .clones = 60, .clone_uniques = 43,
            .views_changed = true, .clones_changed = true,
            .views_delta = 14, .view_uniques_delta = 4,
            .clones_delta = 60, .clone_uniques_delta = 43,
        },
    },
    .total_views              = 195,   // 45+59+25+30+22+14
    .total_view_uniques       = 15,    //  6+ 1+ 2+ 1+ 1+ 4
    .total_clones             = 433,   // 83+68+92+68+62+60
    .total_clone_uniques      = 262,   // 46+31+64+33+45+43
    .total_views_delta        = 195,
    .total_view_uniques_delta = 15,
    .total_clones_delta       = 433,
    .total_clone_uniques_delta= 262,
};

int main(int argc, char **argv)
{
    sim_argc = argc;
    sim_argv = argv;

    board_init();

    // screen_idx: -1 = summary, 0..count-1 = per-repo card
    int screen_idx = -1;
    bool running = true;

    while (running && screencap_poll()) {
        if (screen_idx < 0)
            dashboard_draw_summary(&STATS);
        else
            dashboard_draw_repo(&STATS, screen_idx);
        // Each draw fn calls board_lcd_flush() internally.

        screen_idx++;
        if (screen_idx >= STATS.count) screen_idx = -1;

        // In interactive mode hold each screen for 4 s, polling for quit/P.
        // In headless mode screencap_poll controls the frame count; skip wait.
        if (!screencap_is_headless()) {
            Uint32 deadline = SDL_GetTicks() + 4000;
            while (SDL_GetTicks() < deadline) {
                if (!screencap_poll()) { running = false; break; }
                SDL_Delay(50);
            }
        }
    }

    screencap_destroy();
    return 0;
}
