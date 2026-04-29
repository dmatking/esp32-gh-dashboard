// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// Dashboard renderer for the 720x720 MIPI-DSI display.
// Uses board_interface.h pixel API + font8x16.

#include "dashboard.h"
#include "board_interface.h"
#include "font8x16.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// --- Palette ---
#define C_BG_R   0x0A
#define C_BG_G   0x0A
#define C_BG_B   0x12   // near-black blue-tinted background

#define C_TITLE_R  0xFF
#define C_TITLE_G  0xFF
#define C_TITLE_B  0xFF

#define C_LABEL_R  0x88
#define C_LABEL_G  0x88
#define C_LABEL_B  0x88

#define C_VIEWS_R  0x40
#define C_VIEWS_G  0x80
#define C_VIEWS_B  0xFF   // blue for views

#define C_CLONES_R 0xFF
#define C_CLONES_G 0xA0
#define C_CLONES_B 0x00  // amber for clones

#define C_GREEN_R  0x00
#define C_GREEN_G  0xFF
#define C_GREEN_B  0x44

#define C_STARS_R  0xFF
#define C_STARS_G  0xDD
#define C_STARS_B  0x00

#define C_DIM_R    0x33
#define C_DIM_G    0x33
#define C_DIM_B    0x33

#define W  720
#define H  720

// --- Drawing helpers ---

static void fill_rect(int x, int y, int w, int h,
                      uint8_t r, uint8_t g, uint8_t b)
{
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            board_lcd_set_pixel_rgb(px, py, r, g, b);
}

static void draw_bar(int x, int y, int w, int h,
                     uint32_t value, uint32_t max_value,
                     uint8_t fr, uint8_t fg, uint8_t fb)
{
    int filled = (max_value > 0) ? (int)((uint64_t)value * w / max_value) : 0;
    fill_rect(x, y, filled, h, fr, fg, fb);
    fill_rect(x + filled, y, w - filled, h, C_DIM_R, C_DIM_G, C_DIM_B);
}

// Two-segment bar: total in dim color, unique portion overlaid in bright color.
// Unique segment shown first (left), non-unique in mid tone, rest dark.
static void draw_bar_split(int x, int y, int w, int h,
                           uint32_t total, uint32_t unique, uint32_t max_value,
                           uint8_t fr, uint8_t fg, uint8_t fb)
{
    if (max_value == 0) max_value = 1;
    int total_px  = (int)((uint64_t)total  * w / max_value);
    int unique_px = (int)((uint64_t)unique * w / max_value);
    if (unique_px > total_px) unique_px = total_px;

    // Unique: bright color
    fill_rect(x, y, unique_px, h, fr, fg, fb);
    // Non-unique remainder of total: dim version of the color
    fill_rect(x + unique_px, y, total_px - unique_px, h, fr / 3, fg / 3, fb / 3);
    // Empty (beyond total): background dim
    fill_rect(x + total_px, y, w - total_px, h, C_DIM_R, C_DIM_G, C_DIM_B);
}

static void draw_hline(int x, int y, int w,
                       uint8_t r, uint8_t g, uint8_t b)
{
    fill_rect(x, y, w, 1, r, g, b);
}

static void draw_line(int x0, int y0, int x1, int y1,
                      uint8_t r, uint8_t g, uint8_t b)
{
    int dx =  abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        if (x0 >= 0 && x0 < W && y0 >= 0 && y0 < H)
            board_lcd_set_pixel_rgb(x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Draw a two-series line graph (views=a, clones=b) with linear trend lines.
// gx,gy = top-left of plot area; gw,gh = width/height of plot area.
// Trend drawn first (half-brightness), data lines on top.
static void draw_mini_graph(int gx, int gy, int gw, int gh,
                            const uint32_t *vals_a, uint8_t ar, uint8_t ag, uint8_t ab,
                            const uint32_t *vals_b, uint8_t br, uint8_t bg, uint8_t bb,
                            int n)
{
    if (n <= 0) return;

    uint32_t max_val = 1;
    for (int i = 0; i < n; i++) {
        if (vals_a[i] > max_val) max_val = vals_a[i];
        if (vals_b[i] > max_val) max_val = vals_b[i];
    }

    fill_rect(gx, gy, gw, gh + 1, C_BG_R, C_BG_G, C_BG_B);
    draw_hline(gx, gy + gh, gw, 0x33, 0x33, 0x55);

#define PY(v) (gy + gh - 1 - (int)((uint64_t)(v) * (uint32_t)(gh - 1) / max_val))
#define PX(i) (gx + (n > 1 ? (i) * gw / (n - 1) : gw / 2))

    // Trend lines first (dimmer), data lines on top
    if (n >= 3) {
        int64_t sum_x = 0, sum_ya = 0, sum_yb = 0, sum_xx = 0;
        int64_t sum_xya = 0, sum_xyb = 0;
        for (int i = 0; i < n; i++) {
            sum_x   += i;
            sum_ya  += vals_a[i];
            sum_yb  += vals_b[i];
            sum_xx  += (int64_t)i * i;
            sum_xya += (int64_t)i * vals_a[i];
            sum_xyb += (int64_t)i * vals_b[i];
        }
        int64_t denom  = (int64_t)n * sum_xx - sum_x * sum_x;
        if (denom > 0) {
            int64_t ndenom = (int64_t)n * denom;
            int64_t sna = (int64_t)n * sum_xya - sum_x * sum_ya;
            int64_t snb = (int64_t)n * sum_xyb - sum_x * sum_yb;
            // y_hat at x=0 and x=n-1, using symmetric form:
            // y_hat(0)   = (sum_y*denom - sn*sum_x) / ndenom
            // y_hat(n-1) = (sum_y*denom + sn*sum_x) / ndenom
            int64_t ta0 = (sum_ya * denom - sna * sum_x) / ndenom;
            int64_t ta1 = (sum_ya * denom + sna * sum_x) / ndenom;
            int64_t tb0 = (sum_yb * denom - snb * sum_x) / ndenom;
            int64_t tb1 = (sum_yb * denom + snb * sum_x) / ndenom;
#define CLAMP64(v) ((v) < 0 ? (int64_t)0 : (v) > (int64_t)max_val ? (int64_t)max_val : (v))
            ta0 = CLAMP64(ta0); ta1 = CLAMP64(ta1);
            tb0 = CLAMP64(tb0); tb1 = CLAMP64(tb1);
#undef CLAMP64
            draw_line(PX(0), PY(ta0), PX(n-1), PY(ta1), ar/2, ag/2, ab/2);
            draw_line(PX(0), PY(tb0), PX(n-1), PY(tb1), br/2, bg/2, bb/2);
        }
    }

    for (int i = 0; i + 1 < n; i++) {
        draw_line(PX(i), PY(vals_a[i]), PX(i+1), PY(vals_a[i+1]), ar, ag, ab);
        draw_line(PX(i), PY(vals_b[i]), PX(i+1), PY(vals_b[i+1]), br, bg, bb);
    }

#undef PY
#undef PX
}

static void snfmt_count(char *buf, int buflen, uint32_t n)
{
    if (n >= 1000) snprintf(buf, buflen, "%lu.%luk", (unsigned long)(n/1000), (unsigned long)((n%1000)/100));
    else           snprintf(buf, buflen, "%lu", (unsigned long)n);
}

// --- Screens ---

void dashboard_draw_repo(const gh_stats_t *stats, int idx)
{
    if (idx < 0 || idx >= stats->count) return;
    const gh_repo_t *r = &stats->repos[idx];

    board_lcd_clear();
    fill_rect(0, 0, W, H, C_BG_R, C_BG_G, C_BG_B);

    const int PAD = 40;
    int y = 30;

    // -- Repo number badge (top-right small) --
    char badge[16];
    snprintf(badge, sizeof(badge), "%d/%d", idx + 1, stats->count);
    font_puts_right(W - PAD, y + 4, badge, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);

    // -- Repo name (large, scale 3) --
    font_puts_scaled(PAD, y, r->name, C_TITLE_R, C_TITLE_G, C_TITLE_B, 3);
    y += FONT_H * 3 + 12;

    // -- Stars / forks --
    if (r->stars || r->forks) {
        char meta[48] = "";
        if (r->stars) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "* %d  ", r->stars);
            strncat(meta, tmp, sizeof(meta) - strlen(meta) - 1);
        }
        if (r->forks) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "Y %d", r->forks);
            strncat(meta, tmp, sizeof(meta) - strlen(meta) - 1);
        }
        font_puts_scaled(PAD, y, meta, C_STARS_R, C_STARS_G, C_STARS_B, 2);
        y += FONT_H * 2 + 6;
    }

    // -- Description --
    if (r->desc[0]) {
        // Word-wrap at ~36 chars (scale 2 = 16px wide, 36*16=576px, fits in 640px)
        char line[64];
        int dlen = strlen(r->desc);
        int di   = 0;
        while (di < dlen && y < 260) {
            int take = dlen - di;
            if (take > 36) {
                // Find last space before 36
                take = 36;
                while (take > 12 && r->desc[di + take] != ' ') take--;
            }
            strncpy(line, r->desc + di, take);
            line[take] = '\0';
            font_puts_scaled(PAD, y, line, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
            y += FONT_H * 2 + 4;
            di += take;
            if (di < dlen && r->desc[di] == ' ') di++;
        }
    }

    y = 280;
    draw_hline(PAD, y, W - PAD * 2, 0x33, 0x33, 0x55);
    y += 16;

    // -- Stats section --
    // Max bar width
    const int BAR_X     = PAD + 160;
    const int BAR_W     = W - BAR_X - PAD;
    const int BAR_H     = 18;
    const int ROW_PAD   = 44;

    char numstr[24];
    char uniqstr[32];

    // -- Views row --
    font_puts_scaled(PAD, y, "Views", C_VIEWS_R, C_VIEWS_G, C_VIEWS_B, 2);
    draw_bar_split(BAR_X, y + 2, BAR_W, BAR_H, r->views, r->view_uniques,
                   r->views ? r->views : 1,
                   C_VIEWS_R, C_VIEWS_G, C_VIEWS_B);

    y += BAR_H + 6;
    {
        char delta[16], udelta[16];
        snfmt_count(numstr, sizeof(numstr), r->views);
        snfmt_count(uniqstr, sizeof(uniqstr), r->view_uniques);
        int cx = BAR_X;
        font_puts_scaled(cx, y, numstr, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += strlen(numstr) * FONT_W * 2;
        if (r->views_delta) {
            snprintf(delta, sizeof(delta), "(+%lu)", (unsigned long)r->views_delta);
            font_puts_scaled(cx, y, delta, C_GREEN_R, C_GREEN_G, C_GREEN_B, 2);
            cx += strlen(delta) * FONT_W * 2;
        }
        font_puts_scaled(cx, y, " total  ", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += 8 * FONT_W * 2;
        font_puts_scaled(cx, y, uniqstr, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += strlen(uniqstr) * FONT_W * 2;
        if (r->view_uniques_delta) {
            snprintf(udelta, sizeof(udelta), "(+%lu)", (unsigned long)r->view_uniques_delta);
            font_puts_scaled(cx, y, udelta, C_GREEN_R, C_GREEN_G, C_GREEN_B, 2);
            cx += strlen(udelta) * FONT_W * 2;
        }
        font_puts_scaled(cx, y, " unique", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
    }
    y += FONT_H * 2 + ROW_PAD;

    // -- Clones row --
    font_puts_scaled(PAD, y, "Clones", C_CLONES_R, C_CLONES_G, C_CLONES_B, 2);
    draw_bar_split(BAR_X, y + 2, BAR_W, BAR_H, r->clones, r->clone_uniques,
                   r->clones ? r->clones : 1,
                   C_CLONES_R, C_CLONES_G, C_CLONES_B);

    y += BAR_H + 6;
    {
        char delta[16], udelta[16];
        snfmt_count(numstr, sizeof(numstr), r->clones);
        snfmt_count(uniqstr, sizeof(uniqstr), r->clone_uniques);
        int cx = BAR_X;
        font_puts_scaled(cx, y, numstr, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += strlen(numstr) * FONT_W * 2;
        if (r->clones_delta) {
            snprintf(delta, sizeof(delta), "(+%lu)", (unsigned long)r->clones_delta);
            font_puts_scaled(cx, y, delta, C_GREEN_R, C_GREEN_G, C_GREEN_B, 2);
            cx += strlen(delta) * FONT_W * 2;
        }
        font_puts_scaled(cx, y, " total  ", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += 8 * FONT_W * 2;
        font_puts_scaled(cx, y, uniqstr, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += strlen(uniqstr) * FONT_W * 2;
        if (r->clone_uniques_delta) {
            snprintf(udelta, sizeof(udelta), "(+%lu)", (unsigned long)r->clone_uniques_delta);
            font_puts_scaled(cx, y, udelta, C_GREEN_R, C_GREEN_G, C_GREEN_B, 2);
            cx += strlen(udelta) * FONT_W * 2;
        }
        font_puts_scaled(cx, y, " unique", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
    }
    y += FONT_H * 2 + ROW_PAD;

    // 14-day history graph
    {
        const int GX = PAD, GY = 543, GW = W - PAD * 2, GH = 150;
        draw_hline(GX, GY - 14, GW, 0x22, 0x22, 0x44);
        font_puts_scaled(GX, GY - 12, "14d", C_DIM_R + 0x20, C_DIM_G + 0x20, C_DIM_B + 0x40, 1);
        draw_mini_graph(GX, GY, GW, GH,
                        r->history_views,  C_VIEWS_R,  C_VIEWS_G,  C_VIEWS_B,
                        r->history_clones, C_CLONES_R, C_CLONES_G, C_CLONES_B,
                        HISTORY_DAYS);
    }

    board_lcd_flush();
}

void dashboard_draw_summary(const gh_stats_t *stats)
{
    board_lcd_clear();
    fill_rect(0, 0, W, H, C_BG_R, C_BG_G, C_BG_B);

    const int PAD = 40;
    int y = 40;

    font_puts_scaled(PAD, y, "GitHub Stats", C_TITLE_R, C_TITLE_G, C_TITLE_B, 3);
    y += FONT_H * 3 + 8;
    font_puts_scaled(PAD, y, "dmatking", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
    y += FONT_H * 2 + 30;

    draw_hline(PAD, y, W - PAD * 2, 0x33, 0x33, 0x55);
    y += 24;

    // Tally total stars across all repos
    int total_stars = 0;
    for (int i = 0; i < stats->count; i++) total_stars += stats->repos[i].stars;

    char buf[64];
    snprintf(buf, sizeof(buf), "Repos tracked:   %d", stats->count);
    font_puts_scaled(PAD, y, buf, C_LABEL_R + 0x30, C_LABEL_G + 0x30, C_LABEL_B + 0x30, 2);
    y += FONT_H * 2 + 16;

    const int BAR_X   = PAD + 14 * FONT_W * 2;  // aligns with "Total clones:" label width
    const int BAR_W   = W - BAR_X - PAD;
    const int BAR_H   = 28;
    const int ROW_PAD = 24;

    // -- Total views row --
    font_puts_scaled(PAD, y, "Total views:", C_VIEWS_R, C_VIEWS_G, C_VIEWS_B, 2);
    draw_bar_split(BAR_X, y + 4, BAR_W, BAR_H,
                   stats->total_views, stats->total_view_uniques,
                   stats->total_views ? stats->total_views : 1,
                   C_VIEWS_R, C_VIEWS_G, C_VIEWS_B);
    y += BAR_H + 8;
    {
        char n[16], u[16], d[16], ud[16];
        snfmt_count(n, sizeof(n), stats->total_views);
        snfmt_count(u, sizeof(u), stats->total_view_uniques);
        int cx = BAR_X;
        font_puts_scaled(cx, y, n, C_TITLE_R, C_TITLE_G, C_TITLE_B, 3);
        cx += strlen(n) * FONT_W * 3;
        if (stats->total_views_delta) {
            snprintf(d, sizeof(d), "(+%lu)", (unsigned long)stats->total_views_delta);
            font_puts_scaled(cx, y, d, C_GREEN_R, C_GREEN_G, C_GREEN_B, 3);
            cx += strlen(d) * FONT_W * 3;
        }
        font_puts_scaled(cx, y, "  ", C_LABEL_R, C_LABEL_G, C_LABEL_B, 3);
        cx += 2 * FONT_W * 3;
        font_puts_scaled(cx, y, u, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += strlen(u) * FONT_W * 2;
        if (stats->total_view_uniques_delta) {
            snprintf(ud, sizeof(ud), "(+%lu)", (unsigned long)stats->total_view_uniques_delta);
            font_puts_scaled(cx, y, ud, C_GREEN_R, C_GREEN_G, C_GREEN_B, 2);
            cx += strlen(ud) * FONT_W * 2;
        }
        font_puts_scaled(cx, y, " unique", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
    }
    y += FONT_H * 3 + ROW_PAD;

    // -- Total clones row --
    font_puts_scaled(PAD, y, "Total clones:", C_CLONES_R, C_CLONES_G, C_CLONES_B, 2);
    draw_bar_split(BAR_X, y + 4, BAR_W, BAR_H,
                   stats->total_clones, stats->total_clone_uniques,
                   stats->total_clones ? stats->total_clones : 1,
                   C_CLONES_R, C_CLONES_G, C_CLONES_B);
    y += BAR_H + 8;
    {
        char n[16], u[16], d[16], ud[16];
        snfmt_count(n, sizeof(n), stats->total_clones);
        snfmt_count(u, sizeof(u), stats->total_clone_uniques);
        int cx = BAR_X;
        font_puts_scaled(cx, y, n, C_TITLE_R, C_TITLE_G, C_TITLE_B, 3);
        cx += strlen(n) * FONT_W * 3;
        if (stats->total_clones_delta) {
            snprintf(d, sizeof(d), "(+%lu)", (unsigned long)stats->total_clones_delta);
            font_puts_scaled(cx, y, d, C_GREEN_R, C_GREEN_G, C_GREEN_B, 3);
            cx += strlen(d) * FONT_W * 3;
        }
        font_puts_scaled(cx, y, "  ", C_LABEL_R, C_LABEL_G, C_LABEL_B, 3);
        cx += 2 * FONT_W * 3;
        font_puts_scaled(cx, y, u, C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
        cx += strlen(u) * FONT_W * 2;
        if (stats->total_clone_uniques_delta) {
            snprintf(ud, sizeof(ud), "(+%lu)", (unsigned long)stats->total_clone_uniques_delta);
            font_puts_scaled(cx, y, ud, C_GREEN_R, C_GREEN_G, C_GREEN_B, 2);
            cx += strlen(ud) * FONT_W * 2;
        }
        font_puts_scaled(cx, y, " unique", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
    }
    y += FONT_H * 3 + ROW_PAD;

    // -- Total stars row --
    font_puts_scaled(PAD, y, "Total stars:", C_STARS_R, C_STARS_G, C_STARS_B, 2);
    {
        char n[16];
        snfmt_count(n, sizeof(n), total_stars);
        int cx = BAR_X;
        font_puts_scaled(cx, y, n, C_TITLE_R, C_TITLE_G, C_TITLE_B, 3);
        cx += strlen(n) * FONT_W * 3;
        font_puts_scaled(cx, y + FONT_H, " across all repos", C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
    }
    y += FONT_H * 3 + 24;

    // 14-day history graph
    {
        const int GX = PAD, GY = 543, GW = W - PAD * 2, GH = 150;
        draw_hline(GX, GY - 14, GW, 0x22, 0x22, 0x44);
        font_puts_scaled(GX, GY - 12, "14d", C_DIM_R + 0x20, C_DIM_G + 0x20, C_DIM_B + 0x40, 1);
        draw_mini_graph(GX, GY, GW, GH,
                        stats->history_total_views,  C_VIEWS_R,  C_VIEWS_G,  C_VIEWS_B,
                        stats->history_total_clones, C_CLONES_R, C_CLONES_G, C_CLONES_B,
                        HISTORY_DAYS);
    }

    board_lcd_flush();
}

void dashboard_draw_fetching(void)
{
    board_lcd_clear();
    fill_rect(0, 0, W, H, C_BG_R, C_BG_G, C_BG_B);
    font_puts_scaled(40, H / 2 - FONT_H * 2, "Fetching stats...",
                     C_LABEL_R, C_LABEL_G, C_LABEL_B, 3);
    font_puts_scaled(40, H / 2 + FONT_H * 2, "api.github.com",
                     C_DIM_R + 0x22, C_DIM_G + 0x22, C_DIM_B + 0x22, 2);
    board_lcd_flush();
}

void dashboard_draw_error(const char *msg)
{
    board_lcd_clear();
    fill_rect(0, 0, W, H, C_BG_R, C_BG_G, C_BG_B);
    font_puts_scaled(40, H / 2 - FONT_H * 3, "Error", 0xFF, 0x44, 0x44, 4);
    font_puts_scaled(40, H / 2 + FONT_H * 2, msg,
                     C_LABEL_R, C_LABEL_G, C_LABEL_B, 2);
    board_lcd_flush();
}
