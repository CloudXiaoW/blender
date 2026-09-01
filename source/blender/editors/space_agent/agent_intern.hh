/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "DNA_space_types.h"

namespace blender {

struct ARegion;
struct bContext;
struct wmOperatorType;
struct wmWindow;

namespace ed::agent {

class AgentWebView {
 public:
  virtual ~AgentWebView() = default;

  virtual void load_url(const char *url) = 0;
  virtual void resize(int width, int height) = 0;
  /** Upload the last OSR frame (or keep the native overlay in sync). */
  virtual void draw(const bContext *C, ARegion *region) = 0;
  virtual void mouse_move(int x, int y) = 0;
  virtual void mouse_down(int button) = 0;
  virtual void mouse_up(int button) = 0;
  virtual void wheel(int dy) = 0;
  virtual void key_down(int key, bool ctrl, bool shift, bool alt) = 0;
  virtual void key_up(int key, bool ctrl, bool shift, bool alt) = 0;
  virtual void execute_js(const char *script) = 0;
  virtual void set_visible(bool visible) = 0;
};

AgentWebView *agent_webview_ensure(SpaceAgent *sagent, wmWindow *win);
AgentWebView *agent_webview_get(SpaceAgent *sagent);
void agent_webview_free(SpaceAgent *sagent);

void agent_default_url(char *dst, int dst_max);
/**
 * Bind the Agent WebView URL to the current .blend identity (`project=` query).
 * Reloads the page when the project key changes so the client can restore or
 * create the matching conversation.
 */
void agent_sync_project(SpaceAgent *sagent, wmWindow *win);
/**
 * Reload the Agent page with a one-shot ``new=`` query so the embed client
 * creates a fresh conversation and rebinds it to the current .blend.
 */
void agent_start_new_session(SpaceAgent *sagent, wmWindow *win);

void agent_main_region_draw(const bContext *C, ARegion *region);

}  // namespace ed::agent

void ED_spacetype_agent();
void AGENT_OT_reload(wmOperatorType *ot);

}  // namespace blender
