// Copyright 2026 David M. King
// SPDX-License-Identifier: MIT
#pragma once

// Seed the screencap layout editor with all editable elements from
// cyd_repo_layout. Call once after screencap_init().
void layout_editor_init(void);

// Push the editor's working positions back into cyd_repo_layout. Call
// once per frame before drawing — cheap, no-op when edit mode is off.
void layout_editor_sync(void);
