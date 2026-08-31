/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * CEF Off-Screen Rendering (production Agent WebView).
 *
 * Enable with CMake -DWITH_BLENDER_CEF=ON and link libcef (+ helper process).
 *
 * Pipeline:
 *   HTML → Chromium (CEF windowless) → CefRenderHandler::OnPaint (BGRA)
 *        → GPU texture (webview_texture.cc) → Agent WINDOW region
 *
 * Blender menus/popovers are blitted after region contents, so OSR content
 * participates in Blender's z-order correctly (unlike a native NSView overlay).
 *
 * Until libcef is vendored, #agent_webview_create_cef returns nullptr and the
 * host falls back to the Cocoa WKWebView overlay (popup-aware hide) or stub.
 */

#include "agent_intern.hh"
#include "webview/webview.hh"

#include "BLI_rect.hh"

#include "DNA_screen_types.h"

#include "GPU_texture.hh"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef WITH_BLENDER_CEF
/* Expected CEF headers once WITH_BLENDER_CEF pulls in the SDK:
 *   #include "include/cef_app.h"
 *   #include "include/cef_browser.h"
 *   #include "include/cef_client.h"
 *   #include "include/cef_render_handler.h"
 * Wire CefRenderHandler::OnPaint → pixels_ below, then upload in draw().
 */
#endif

namespace blender::ed::agent {

#ifdef WITH_BLENDER_CEF

class CefOSRWebView : public AgentWebView {
 public:
  void load_url(const char *url) override
  {
    url_ = url ? url : "";
    /* TODO: browser_->GetMainFrame()->LoadURL(url_); */
  }

  void resize(int width, int height) override
  {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    {
      std::lock_guard lock(mutex_);
      pixels_.assign(size_t(width_) * size_t(height_) * 4, 0);
    }
    /* TODO: browser_->GetHost()->WasResized(); */
  }

  void draw(const bContext * /*C*/, ARegion *region) override
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
    if (tex == nullptr) {
      return;
    }
    {
      std::lock_guard lock(mutex_);
      if (!pixels_.empty()) {
        GPU_texture_update(tex, GPU_DATA_UBYTE, pixels_.data());
      }
    }
    agent_webview_texture_draw(tex, region);
  }

  void mouse_move(int x, int y) override
  {
    (void)x;
    (void)y;
    /* TODO: CefMouseEvent → SendMouseMoveEvent */
  }
  void mouse_down(int button) override
  {
    (void)button;
  }
  void mouse_up(int button) override
  {
    (void)button;
  }
  void wheel(int dy) override
  {
    (void)dy;
  }
  void key_down(int key, bool ctrl, bool shift, bool alt) override
  {
    (void)key;
    (void)ctrl;
    (void)shift;
    (void)alt;
  }
  void key_up(int key, bool ctrl, bool shift, bool alt) override
  {
    (void)key;
    (void)ctrl;
    (void)shift;
    (void)alt;
  }
  void execute_js(const char *script) override
  {
    (void)script;
  }
  void set_visible(bool /*visible*/) override {}

  /** CefRenderHandler::OnPaint copies BGRA into #pixels_ under #mutex_. */
  void on_paint_bgra(const void *buffer, int width, int height)
  {
    std::lock_guard lock(mutex_);
    if (width != width_ || height != height_) {
      return;
    }
    const size_t nbytes = size_t(width) * size_t(height) * 4;
    if (pixels_.size() != nbytes) {
      pixels_.resize(nbytes);
    }
    std::memcpy(pixels_.data(), buffer, nbytes);
  }

  ~CefOSRWebView() override
  {
    agent_webview_texture_free(&texture_);
  }

 private:
  std::string url_;
  int width_ = 1;
  int height_ = 1;
  std::mutex mutex_;
  std::vector<uint8_t> pixels_;
  gpu::Texture *texture_ = nullptr;
};

std::unique_ptr<AgentWebView> agent_webview_create_cef()
{
  return std::make_unique<CefOSRWebView>();
}

#else

std::unique_ptr<AgentWebView> agent_webview_create_cef()
{
  return nullptr;
}

#endif

}  // namespace blender::ed::agent
