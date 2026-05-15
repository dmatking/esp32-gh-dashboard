// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// Data-driven layout for the CYD 320x240 per-repo dashboard screen.
//
// Each element captures position + style. The renderer in dashboard.c
// reads these values instead of using hardcoded x/y arithmetic, so the
// layout can be edited externally (e.g. dragged in the sim) without
// touching the draw code.
//
// Element x/y are in display pixels with origin at top-left. Top-zone
// elements (title/clock/stars/desc) define a *starting* position; the
// renderer flows the next element below the previous one when the
// previous is present. Stats-pane elements have absolute positions so
// the Views/Clones bars stay put regardless of top content height.

#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int     x, y;
    uint8_t scale;
    uint8_t r, g, b;
    bool    right_align;  // when true, x is the right edge
} layout_text_t;

typedef struct {
    int x, y, w, h;
} layout_rect_t;

typedef struct {
    // Top zone — variable height; renderer flows items downward.
    layout_text_t title;
    layout_text_t clock;        // right_align is set
    layout_text_t stars;        // shown only if stars/forks > 0
    layout_text_t desc;         // first line; line 2 (if any) sits below

    // Stats pane — absolute positions, stable across repos.
    layout_text_t views_label;
    layout_text_t views_bignum;
    layout_text_t views_delta;
    layout_rect_t views_bar;
    layout_text_t views_uniq;

    layout_text_t clones_label;
    layout_text_t clones_bignum;
    layout_text_t clones_delta;
    layout_rect_t clones_bar;
    layout_text_t clones_uniq;
} layout_cyd_repo_t;

/* Non-const so the desktop sim's layout editor can mutate positions in
 * place. Firmware code only reads from it. */
extern layout_cyd_repo_t cyd_repo_layout;
