// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
#pragma once

#include "github_api.h"

// Draw a full-screen repo stats card for stats->repos[idx].
void dashboard_draw_repo(const gh_stats_t *stats, int idx);

// Draw the summary screen (totals across all repos).
void dashboard_draw_summary(const gh_stats_t *stats);

// Draw a "fetching..." splash screen.
void dashboard_draw_fetching(void);

// Draw an error screen.
void dashboard_draw_error(const char *msg);

// Draw the WiFi provisioning screen.  title is shown at the top (e.g. "WiFi Setup"
// or "Wrong Password?"); ssid is the AP name the user should connect to.
void dashboard_draw_provisioning(const char *title, const char *ssid);
