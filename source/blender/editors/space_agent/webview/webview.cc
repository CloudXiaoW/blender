/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "agent_intern.hh"
#include "webview/webview.hh"

#include "BKE_main.hh"

#include "BLI_hash_mm2a.hh"
#include "BLI_map.hh"
#include "BLI_rect.hh"
#include "BLI_string.hh"
#include "BLI_time.hh"

#include "GPU_immediate.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "DNA_screen_types.h"

#include "ED_screen.hh"

#include "MEM_guardedalloc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace blender::ed::agent {

static Map<const SpaceAgent *, std::unique_ptr<AgentWebView>> g_webviews;

/** Opaque key shared with the embed client (`?project=`). */
static constexpr const char *AGENT_PROJECT_UNSAVED = "__unsaved__";

static void trim_url_whitespace(std::string &url)
{
  while (!url.empty() && (url.back() == '\n' || url.back() == '\r' || url.back() == ' ')) {
    url.pop_back();
  }
}

/** Ensure the Agent embed query is present (auth file may already include it). */
static std::string ensure_embed_query(std::string url)
{
  trim_url_whitespace(url);
  if (url.empty()) {
    return url;
  }
  if (url.find("embed=") == std::string::npos) {
    url += (url.find('?') == std::string::npos) ? "?embed=blender-agent" :
                                                  "&embed=blender-agent";
  }
  return url;
}

static std::string percent_encode(const std::string &value)
{
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~' || c == '/')
    {
      out.push_back(char(c));
    }
    else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out.append(buf);
    }
  }
  return out;
}

static std::string query_value(const std::string &url, const char *key)
{
  const std::string needle = std::string(key) + "=";
  const size_t q = url.find('?');
  if (q == std::string::npos) {
    return {};
  }
  size_t pos = q + 1;
  while (pos < url.size()) {
    const size_t amp = url.find('&', pos);
    const size_t end = (amp == std::string::npos) ? url.size() : amp;
    if (url.compare(pos, needle.size(), needle) == 0) {
      return url.substr(pos + needle.size(), end - (pos + needle.size()));
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  return {};
}

static std::string strip_query_key(std::string url, const char *key)
{
  const std::string needle = std::string(key) + "=";
  const size_t q = url.find('?');
  if (q == std::string::npos) {
    return url;
  }
  std::string out = url.substr(0, q);
  std::string kept;
  size_t pos = q + 1;
  while (pos < url.size()) {
    const size_t amp = url.find('&', pos);
    const size_t end = (amp == std::string::npos) ? url.size() : amp;
    const std::string part = url.substr(pos, end - pos);
    if (part.rfind(needle, 0) != 0) {
      if (!kept.empty()) {
        kept.push_back('&');
      }
      kept.append(part);
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  if (!kept.empty()) {
    out.push_back('?');
    out.append(kept);
  }
  return out;
}

static std::string current_project_key()
{
  const char *path = BKE_main_blendfile_path_from_global();
  if (path == nullptr || path[0] == '\0') {
    return AGENT_PROJECT_UNSAVED;
  }
  return path;
}

/**
 * Attach/replace `project=` so the embed client can bind conversations per .blend.
 * Falls back to a short hash when the absolute path would overflow #SPACE_AGENT_URL_MAX.
 */
static std::string apply_project_query(std::string url, const std::string &project_key)
{
  url = ensure_embed_query(std::move(url));
  url = strip_query_key(std::move(url), "project");
  const std::string encoded = percent_encode(project_key);
  std::string with_path = url;
  with_path += (with_path.find('?') == std::string::npos) ? "?project=" : "&project=";
  with_path.append(encoded);
  if (int(with_path.size()) < SPACE_AGENT_URL_MAX) {
    return with_path;
  }
  /* Path too long for DNA url[] — use a stable short fingerprint. */
  BLI_HashMurmur2A hasher;
  BLI_hash_mm2a_init(&hasher, 0);
  BLI_hash_mm2a_add(&hasher,
                    reinterpret_cast<const unsigned char *>(project_key.data()),
                    project_key.size());
  const uint32_t digest = BLI_hash_mm2a_end(&hasher);
  char short_key[32];
  snprintf(short_key, sizeof(short_key), "h%08x", digest);
  std::string with_hash = url;
  with_hash += (with_hash.find('?') == std::string::npos) ? "?project=" : "&project=";
  with_hash.append(short_key);
  return with_hash;
}

/**
 * Path written by vibe3d/plugin when dsh web becomes ready:
 * `$DSH_HOME/profiles/$DSH_PROFILE/agent-auth.url` (defaults: ~/.dsh, vibe3d).
 */
static bool read_agent_auth_url_file(std::string &out)
{
  const char *dsh_home_env = std::getenv("DSH_HOME");
  const char *profile_env = std::getenv("DSH_PROFILE");
  const char *home = std::getenv("HOME");
  std::string path;
  if (dsh_home_env && dsh_home_env[0]) {
    path = dsh_home_env;
  }
  else if (home && home[0]) {
    path = std::string(home) + "/.dsh";
  }
  else {
    return false;
  }
  path += "/profiles/";
  path += (profile_env && profile_env[0]) ? profile_env : "vibe3d";
  path += "/agent-auth.url";

  std::ifstream in(path);
  if (!in) {
    return false;
  }
  std::string line;
  if (!std::getline(in, line)) {
    return false;
  }
  line = ensure_embed_query(std::move(line));
  if (line.empty() || line.rfind("http", 0) != 0) {
    return false;
  }
  out = std::move(line);
  return true;
}

void agent_default_url(char *dst, int dst_max)
{
  std::string url;
  const char *env = std::getenv("VIBE3D_AGENT_URL");
  if (env && env[0]) {
    url = ensure_embed_query(env);
  }
  else if (!read_agent_auth_url_file(url)) {
    url = AGENT_DEFAULT_URL;
  }
  url = apply_project_query(std::move(url), current_project_key());
  BLI_strncpy(dst, url.c_str(), dst_max);
}

void agent_sync_project(SpaceAgent *sagent, wmWindow *win)
{
  if (sagent == nullptr) {
    return;
  }
  char next_url[SPACE_AGENT_URL_MAX];
  agent_default_url(next_url, SPACE_AGENT_URL_MAX);
  const std::string next_project = query_value(next_url, "project");
  const std::string cur_project = query_value(sagent->url, "project");
  if (next_project == cur_project && sagent->url[0] != '\0') {
    return;
  }
  BLI_strncpy(sagent->url, next_url, SPACE_AGENT_URL_MAX);
  if (AgentWebView *view = agent_webview_get(sagent)) {
    view->load_url(sagent->url);
  }
  else if (win != nullptr) {
    agent_webview_ensure(sagent, win);
  }
}

void agent_start_new_session(SpaceAgent *sagent, wmWindow *win)
{
  if (sagent == nullptr) {
    return;
  }
  agent_default_url(sagent->url, SPACE_AGENT_URL_MAX);
  std::string load = strip_query_key(sagent->url, "new");
  char nonce[32];
  snprintf(nonce, sizeof(nonce), "%lld", static_cast<long long>(BLI_time_now_seconds() * 1000.0));
  load += (load.find('?') == std::string::npos) ? "?new=" : "&new=";
  load.append(nonce);
  if (AgentWebView *view = agent_webview_ensure(sagent, win)) {
    view->load_url(load.c_str());
  }
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
  /* Auth URL + project= so the embed client can restore/create the right chat. */
  agent_default_url(sagent->url, SPACE_AGENT_URL_MAX);
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
