// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
//
// Fetches and parses latest.csv and totals.csv from github-traffic-log repo.
// latest.csv: per-day actual counts for the two most recent dates.
// totals.csv: cumulative all-time sums per repo.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "github_api.h"

// --- latest.csv (daily counts, last 2 dates) ---

#define CSV_MAX_ROWS 128  // 2 dates × up to 64 repos

typedef struct {
    char     date[12];               // "YYYY-MM-DD"
    char     repo[GH_REPO_NAME_LEN];
    uint32_t views;
    uint32_t view_uniques;
    uint32_t clones;
    uint32_t clone_uniques;
    int      stars;
    int      forks;
} csv_row_t;

typedef struct {
    csv_row_t rows[CSV_MAX_ROWS];
    int       count;
    char      newest_date[12];   // most recent date in file
    char      prev_date[12];     // second most recent date (may be empty)
} csv_data_t;

// Fetch and parse latest.csv.  Returns true on success.
bool traffic_csv_fetch(csv_data_t *out);

// Find the row for a given date + repo name.  Returns NULL if not found.
const csv_row_t *traffic_csv_find(const csv_data_t *data,
                                  const char *date, const char *repo);

// --- totals.csv (cumulative all-time sums per repo) ---

typedef struct {
    char     repo[GH_REPO_NAME_LEN];
    uint32_t views;
    uint32_t view_uniques;
    uint32_t clones;
    uint32_t clone_uniques;
    int      stars;
    int      forks;
} totals_row_t;

typedef struct {
    totals_row_t rows[GH_MAX_REPOS];
    int          count;
} totals_data_t;

// Fetch and parse totals.csv.  Returns true on success.
bool traffic_totals_fetch(totals_data_t *out);

// Find the totals row for a given repo name.  Returns NULL if not found.
const totals_row_t *traffic_totals_find(const totals_data_t *data,
                                        const char *repo);
