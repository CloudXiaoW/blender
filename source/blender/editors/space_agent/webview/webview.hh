/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <memory>
#include <string>

#include "DNA_space_types.h"

struct ARegion;
struct bContext;
struct wmWindow;

namespace blender::gpu {
class Texture;
}

namespace blender::ed::agent {

/** Host-level JS injected into the conversation page. Mesh edits stay on MCP. */
inline constexpr const char *AGENT_HOST_JS = R"JS(
window.blender = window.blender || {
  host: 'blender-agent-space',
  getTheme: function () { return 'blender'; },
  focusViewport: function () {},
  openFile: function () {},
};
)JS";

inline constexpr const char *AGENT_DEFAULT_URL = "http://127.0.0.1:3080/?embed=blender-agent";

std::unique_ptr<class AgentWebView> agent_webview_create();

void agent_webview_texture_draw(gpu::Texture *texture, const ARegion *region);
gpu::Texture *agent_webview_texture_ensure(gpu::Texture **slot, int width, int height);
void agent_webview_texture_free(gpu::Texture **slot);

}  // namespace blender::ed::agent
