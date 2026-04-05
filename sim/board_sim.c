// board_sim.c — desktop implementation of board_interface.h using screencap.
// Replaces board_waveshare_wvshr_p4_720_touch.c for the native sim build.

#include "board_interface.h"
#include "screencap.h"
#include <string.h>
#include <stdint.h>

#define LCD_W  720
#define LCD_H  720
#define BPP    3   // RGB888

// Static framebuffer — 720*720*3 = ~1.5 MB, fine on desktop
static uint8_t s_buf[LCD_H * LCD_W * BPP];

// Forwarded from main_sim.c so screencap_init gets real argc/argv
extern int   sim_argc;
extern char **sim_argv;

void board_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));

    screencap_cfg_t cfg = {
        .width    = LCD_W,
        .height   = LCD_H,
        .format   = SCREENCAP_RGB888,
        .scale    = 1,
        .title    = "esp32-gh-dashboard",
        .framebuf = s_buf,
    };
    screencap_init(&cfg, sim_argc, sim_argv);
}

const char *board_get_name(void) { return "Sim 720x720"; }
bool        board_has_lcd(void)  { return true; }

int board_lcd_width(void)  { return LCD_W; }
int board_lcd_height(void) { return LCD_H; }

void board_lcd_clear(void)
{
    memset(s_buf, 0, sizeof(s_buf));
}

void board_lcd_flush(void)
{
    screencap_flush();
}

void board_lcd_fill(uint16_t color)
{
    uint8_t r, g, b;
    board_lcd_unpack_rgb(color, &r, &g, &b);
    uint8_t *p = s_buf;
    for (int i = 0; i < LCD_W * LCD_H; i++, p += BPP) {
        p[0] = r; p[1] = g; p[2] = b;
    }
    board_lcd_flush();
}

// Note: hardware board stores BGR (for the ST7703 panel).
// Here we store RGB — screencap's SCREENCAP_RGB888 expects R at byte 0.
void board_lcd_set_pixel_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
    uint8_t *p = s_buf + (y * LCD_W + x) * BPP;
    p[0] = r; p[1] = g; p[2] = b;
}

void board_lcd_set_pixel_raw(int x, int y, uint16_t color)
{
    uint8_t r, g, b;
    board_lcd_unpack_rgb(color, &r, &g, &b);
    board_lcd_set_pixel_rgb(x, y, r, g, b);
}

uint16_t board_lcd_pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) |
           ((uint16_t)(g >> 2) <<  5) |
           ((uint16_t)(b >> 3));
}

uint16_t board_lcd_get_pixel_raw(int x, int y)
{
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return 0;
    uint8_t *p = s_buf + (y * LCD_W + x) * BPP;
    return board_lcd_pack_rgb(p[0], p[1], p[2]);
}

void board_lcd_unpack_rgb(uint16_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((color >> 11) & 0x1F) << 3;
    *g = ((color >>  5) & 0x3F) << 2;
    *b = ( color        & 0x1F) << 3;
}

void board_lcd_sanity_test(void) { /* no-op in sim */ }
