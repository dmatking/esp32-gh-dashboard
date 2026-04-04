// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GH_MAX_REPOS     6   // GitHub allows at most 6 pinned repos
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
    bool     views_changed;     // true if views increased since last fetch
    bool     clones_changed;
    uint32_t views_delta;          // how much views increased (0 if unchanged)
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
} gh_stats_t;

// Fetch stats for all repos owned by CONFIG_GH_USERNAME.
// Returns true on success. Populates stats and sets *_changed flags
// by comparing against prev (may be NULL for first fetch).
bool github_fetch_stats(gh_stats_t *stats, const gh_stats_t *prev);
