// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// Default CYD 320x240 per-repo layout. Reproduces the hand-tuned
// positions from before the data-driven refactor — pixel-identical to
// the previous inline arithmetic.

#include "layout_cyd.h"

// Color shorthands matching dashboard.c
#define TITLE   0xFF, 0xFF, 0xFF
#define LABEL   0x88, 0x88, 0x88
#define STARS   0xFF, 0xDD, 0x00
#define VIEWS   0x40, 0x80, 0xFF
#define CLONES  0xFF, 0xA0, 0x00
#define GREEN   0x00, 0xFF, 0x44

layout_cyd_repo_t cyd_repo_layout = {
    // ----- Top zone (flows downward; y on stars/desc is the
    // starting offset used when each item is present) -----
    .title  = { .x =   4, .y =  4, .scale = 1, .r = TITLE,  .right_align = false },
    .clock  = { .x = 316, .y =  4, .scale = 1, .r = LABEL,  .right_align = true  },
    .stars  = { .x =   4, .y = 24, .scale = 1, .r = STARS                          },
    .desc   = { .x =   4, .y = 44, .scale = 1, .r = LABEL                          },

    // ----- Stats pane (absolute) -----
    // Views label sits at y=88; the big number is offset upward by 8 px
    // so its visual baseline aligns with the label.
    .views_label  = { .x =  4, .y =  88, .scale = 1, .r = VIEWS  },
    .views_bignum = { .x = 60, .y =  80, .scale = 2, .r = VIEWS  },
    .views_delta  = { .x = 96, .y =  88, .scale = 1, .r = GREEN  },
    .views_bar    = { .x =  4, .y = 122, .w = 312, .h = 12 },
    .views_uniq   = { .x =  4, .y = 136, .scale = 1, .r = LABEL  },

    .clones_label  = { .x =  4, .y = 158, .scale = 1, .r = CLONES },
    .clones_bignum = { .x = 60, .y = 150, .scale = 2, .r = CLONES },
    .clones_delta  = { .x = 96, .y = 158, .scale = 1, .r = GREEN  },
    .clones_bar    = { .x =  4, .y = 192, .w = 312, .h = 12 },
    .clones_uniq   = { .x =  4, .y = 206, .scale = 1, .r = LABEL  },
};
