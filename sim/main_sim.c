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
#include <sys/select.h>
#include <unistd.h>

#include "board_interface.h"
#include "dashboard.h"
#include "github_api.h"
#include "screencap.h"
#include "layout_editor.h"

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
            .views_delta = 4, .view_uniques_delta = 3,
            .clones_delta = 7, .clone_uniques_delta = 5,
            .history_views  = {0,1,2,1,3,2,4,3,2,5,3,4,6,5},
            .history_clones = {2,3,4,5,3,6,4,7,5,8,6,9,7,10},
        },
        {
            .name             = "esp32-idf-new",
            .desc             = "ESP-IDF new-project scaffolding tool",
            .stars = 0, .forks = 0,
            .views = 59, .view_uniques = 1,
            .clones = 68, .clone_uniques = 31,
            .views_changed = true, .clones_changed = true,
            .views_delta = 0, .view_uniques_delta = 0,
            .clones_delta = 0, .clone_uniques_delta = 0,
            .history_views  = {3,2,5,4,6,3,7,5,4,8,6,5,9,7},
            .history_clones = {4,5,3,6,4,5,7,4,6,5,8,6,7,9},
        },
        {
            .name             = "esp32-video-stream",
            .desc             = "MJPEG video streaming over WiFi",
            .stars = 0, .forks = 0,
            .views = 25, .view_uniques = 2,
            .clones = 92, .clone_uniques = 64,
            .views_changed = true, .clones_changed = true,
            .views_delta = 0, .view_uniques_delta = 0,
            .clones_delta = 0, .clone_uniques_delta = 0,
            .history_views  = {1,0,2,1,0,3,1,2,0,1,3,2,1,2},
            .history_clones = {5,7,6,8,5,9,6,10,7,11,8,12,9,13},
        },
        {
            .name             = "esp32-p4-webradio",
            .desc             = "Internet radio on ESP32-P4 with LCD",
            .stars = 0, .forks = 0,
            .views = 30, .view_uniques = 1,
            .clones = 68, .clone_uniques = 33,
            .views_changed = true, .clones_changed = true,
            .views_delta = 0, .view_uniques_delta = 0,
            .clones_delta = 0, .clone_uniques_delta = 0,
            .history_views  = {2,3,1,4,2,3,5,2,4,3,6,4,5,7},
            .history_clones = {3,4,3,5,4,5,4,6,5,4,6,5,7,6},
        },
        {
            .name             = "esp32-gh-dashboard",
            .desc             = "GitHub stats dashboard on ESP32-P4",
            .stars = 0, .forks = 0,
            .views = 22, .view_uniques = 1,
            .clones = 62, .clone_uniques = 45,
            .views_changed = true, .clones_changed = true,
            .views_delta = 0, .view_uniques_delta = 0,
            .clones_delta = 0, .clone_uniques_delta = 0,
            .history_views  = {1,1,2,1,3,2,1,4,2,3,5,3,4,6},
            .history_clones = {3,4,5,4,6,5,7,5,8,6,9,7,10,8},
        },
        {
            .name             = "esp32-p4-demos",
            .desc             = "Demo collection for ESP32-P4 devkit",
            .stars = 1, .forks = 0,
            .views = 14, .view_uniques = 4,
            .clones = 60, .clone_uniques = 43,
            .views_changed = true, .clones_changed = true,
            .views_delta = 0, .view_uniques_delta = 0,
            .clones_delta = 0, .clone_uniques_delta = 0,
            .history_views  = {4,3,5,4,6,5,4,7,5,6,8,6,7,9},
            .history_clones = {4,5,4,6,5,6,5,7,6,5,7,6,8,7},
        },
    },
    .total_views              = 195,   // 45+59+25+30+22+14
    .total_view_uniques       = 15,    //  6+ 1+ 2+ 1+ 1+ 4
    .total_clones             = 433,   // 83+68+92+68+62+60
    .total_clone_uniques      = 262,   // 46+31+64+33+45+43
    .total_views_delta        = 4,
    .total_view_uniques_delta = 3,
    .total_clones_delta       = 7,
    .total_clone_uniques_delta= 5,
    // daily totals across all 6 repos (sum of per-repo history above)
    .history_total_views  = {11,10,17,15,20,15,21,21,17,26,31,24,32,36},
    .history_total_clones = {21,28,25,34,27,36,33,39,37,39,44,45,48,53},
};

int main(int argc, char **argv)
{
    sim_argc = argc;
    sim_argv = argv;

    board_init();
    layout_editor_init();

    // screen_idx: -1 = summary, 0..count-1 = per-repo card
    int screen_idx = -1;
    bool running = true;

    // Helper: render the current screen (summary or per-repo) using the
    // latest layout. Used by both the main loop and the inner wait loop
    // so the display updates live while in edit mode.
    #define REDRAW() do {                                       \
        layout_editor_sync();                                   \
        if (screen_idx < 0) dashboard_draw_summary(&STATS);     \
        else                dashboard_draw_repo(&STATS, screen_idx); \
    } while (0)

    while (running && screencap_poll()) {
        REDRAW();
        // Each draw fn calls board_lcd_flush() internally.

        // Interactive: wait for Enter on stdin before advancing.
        // SDL events (Esc/P/G/click/edit drags) keep working in the
        // window; in edit mode we redraw every poll so drags are live.
        if (!screencap_is_headless()) {
            printf("[Enter] next screen, Esc in window to quit\n");
            fflush(stdout);
            bool advance = false;
            while (!advance) {
                if (!screencap_poll()) { running = false; break; }
                if (screencap_editor_active()) REDRAW();
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(STDIN_FILENO, &rfds);
                struct timeval tv = { 0, 50000 };  // 50 ms
                if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0
                    && FD_ISSET(STDIN_FILENO, &rfds)) {
                    int c;
                    while ((c = getchar()) != '\n' && c != EOF) { }
                    advance = true;
                }
            }
        }

        screen_idx++;
        if (screen_idx >= STATS.count) screen_idx = -1;
    }

    #undef REDRAW
    screencap_destroy();
    return 0;
}
