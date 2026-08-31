/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "agent_intern.hh"
#include "webview/webview.hh"

#include "BLI_map.hh"
#include "BLI_rect.hh"
#include "BLI_string.hh"

#include "GPU_immediate.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "DNA_screen_types.h"

#include "ED_screen.hh"

#include "MEM_guardedalloc.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

namespace blender::ed::agent {

static Map<const SpaceAgent *, std::unique_ptr<AgentWebView>> g_webviews;

void agent_default_url(char *dst, int dst_max)
{
  const char *env = std::getenv("VIBE3D_AGENT_URL");
  BLI_strncpy(dst, (env && env[0]) ? env : AGENT_DEFAULT_URL, dst_max);
}

/** Software placeholder until CEF OSR is linked. Fills a dark BGRA buffer. */
class StubWebView : public AgentWebView {
 public:
  void load_url(const char *url) override
  {
    url_ = url ? url : "";
  }
  void resize(int width, int height) override
  {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    pixels_.assign(size_t(width_) * size_t(height_) * 4, 0);
    for (int i = 0; i < width_ * height_; i++) {
      pixels_[i * 4 + 0] = 28;
      pixels_[i * 4 + 1] = 28;
      pixels_[i * 4 + 2] = 32;
      pixels_[i * 4 + 3] = 255;
    }
  }
  void draw(const bContext * /*C*/, ARegion *region) override;
  void mouse_move(int /*x*/, int /*y*/) override {}
  void mouse_down(int /*button*/) override {}
  void mouse_up(int /*button*/) override {}
  void wheel(int /*dy*/) override {}
  void key_down(int /*key*/, bool /*ctrl*/, bool /*shift*/, bool /*alt*/) override {}
  void key_up(int /*key*/, bool /*ctrl*/, bool /*shift*/, bool /*alt*/) override {}
  void execute_js(const char * /*script*/) override {}
  void set_visible(bool /*visible*/) override {}

  std::string url_;
  int width_ = 1;
  int height_ = 1;
  std::vector<uint8_t> pixels_;
  gpu::Texture *texture_ = nullptr;
  ~StubWebView() override
  {
    agent_webview_texture_free(&texture_);
  }
};

std::unique_ptr<AgentWebView> agent_webview_create_cocoa();
std::unique_ptr<AgentWebView> agent_webview_create_cef();

std::unique_ptr<AgentWebView> agent_webview_create()
{
#ifdef WITH_BLENDER_CEF
  return agent_webview_create_cef();
#elif defined(__APPLE__)
  if (std::unique_ptr<AgentWebView> cocoa = agent_webview_create_cocoa()) {
    return cocoa;
  }
#endif
  return std::make_unique<StubWebView>();
}

AgentWebView *agent_webview_get(SpaceAgent *sagent)
{
  if (sagent == nullptr) {
    return nullptr;
  }
  std::unique_ptr<AgentWebView> *existing = g_webviews.lookup_ptr(sagent);
  return existing ? existing->get() : nullptr;
}

AgentWebView *agent_webview_ensure(SpaceAgent *sagent, wmWindow * /*win*/)
{
  if (sagent == nullptr) {
    return nullptr;
  }
  if (AgentWebView *existing = agent_webview_get(sagent)) {
    return existing;
  }
  std::unique_ptr<AgentWebView> view = agent_webview_create();
  if (sagent->url[0] == '\0') {
    agent_default_url(sagent->url, SPACE_AGENT_URL_MAX);
  }
  view->load_url(sagent->url);
  AgentWebView *ptr = view.get();
  g_webviews.add(sagent, std::move(view));
  return ptr;
}

void agent_webview_free(SpaceAgent *sagent)
{
  if (sagent) {
    g_webviews.remove(sagent);
  }
}

void StubWebView::draw(const bContext * /*C*/, ARegion *region)
{
  if (region == nullptr) {
    return;
  }
  const int w = std::max(1, BLI_rcti_size_x(&region->winrct));
  const int h = std::max(1, BLI_rcti_size_y(&region->winrct));
  if (w != width_ || h != height_) {
    resize(w, h);
  }
  gpu::Texture *tex = agent_webview_texture_ensure(&texture_, width_, height_);
  if (tex) {
    GPU_texture_update(tex, GPU_DATA_UBYTE, pixels_.data());
    agent_webview_texture_draw(tex, region);
  }
}

#ifndef __APPLE__
std::unique_ptr<AgentWebView> agent_webview_create_cocoa()
{
  return nullptr;
}
#endif

#ifndef WITH_BLENDER_CEF
std::unique_ptr<AgentWebView> agent_webview_create_cef()
{
  return nullptr;
}
#endif

}  // namespace blender::ed::agent
