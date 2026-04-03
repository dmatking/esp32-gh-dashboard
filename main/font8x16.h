// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Minimal 8x16 bitmap font for ASCII 32–126.
// Each character is 16 bytes (one byte per row, MSB = leftmost pixel).

#pragma once

#include <stdint.h>
#include "board_interface.h"

#define FONT_W 8
#define FONT_H 16

extern const uint8_t font8x16_data[];

// Draw a character at (x,y) with given RGB color, optional integer scale.
static inline void font_putc_scaled(int x, int y, char c,
                                    uint8_t r, uint8_t g, uint8_t b,
                                    int scale)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = &font8x16_data[(c - 32) * FONT_H];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        board_lcd_set_pixel_rgb(x + col * scale + sx,
                                               y + row * scale + sy,
                                               r, g, b);
            }
        }
    }
}

static inline void font_putc(int x, int y, char c,
                              uint8_t r, uint8_t g, uint8_t b)
{
    font_putc_scaled(x, y, c, r, g, b, 1);
}

static inline void font_puts_scaled(int x, int y, const char *s,
                                    uint8_t r, uint8_t g, uint8_t b,
                                    int scale)
{
    int cx = x;
    while (*s) {
        font_putc_scaled(cx, y, *s, r, g, b, scale);
        cx += FONT_W * scale;
        s++;
    }
}

static inline void font_puts(int x, int y, const char *s,
                              uint8_t r, uint8_t g, uint8_t b)
{
    font_puts_scaled(x, y, s, r, g, b, 1);
}

// Draw a right-aligned string ending at x.
static inline void font_puts_right(int x_right, int y, const char *s,
                                   uint8_t r, uint8_t g, uint8_t b, int scale)
{
    int len = 0;
    for (const char *p = s; *p; p++) len++;
    int x = x_right - len * FONT_W * scale;
    font_puts_scaled(x, y, s, r, g, b, scale);
}
