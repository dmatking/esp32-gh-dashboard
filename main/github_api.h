// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GH_MAX_REPOS     32  // max repos to display
#define GH_REPO_NAME_LEN 48
#define GH_DESC_LEN      128

typedef struct {
    char     name[GH_REPO_NAME_LEN];
    char     desc[GH_DESC_LEN];
    int      stars;
    int      forks;
    bool     is_private;
    uint32_t views;
    uint32_t view_uniques;
    uint32_t clones;
    uint32_t clone_uniques;
    bool     views_changed;
    bool     clones_changed;
    uint32_t views_delta;
    uint32_t view_uniques_delta;
    uint32_t clones_delta;
    uint32_t clone_uniques_delta;
} gh_repo_t;

typedef struct {
    gh_repo_t repos[GH_MAX_REPOS];
    int       count;
    uint32_t  total_views;
    uint32_t  total_view_uniques;
    uint32_t  total_clones;
    uint32_t  total_clone_uniques;
    uint32_t  total_views_delta;
    uint32_t  total_view_uniques_delta;
    uint32_t  total_clones_delta;
    uint32_t  total_clone_uniques_delta;
} gh_stats_t;

// Fetch all user repos via GraphQL, traffic via CSV.
// Deltas are computed day-over-day from latest.csv (no prev needed).
bool github_fetch_stats(gh_stats_t *stats);
