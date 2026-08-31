/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "agent_intern.hh"
#include "webview/webview.hh"

#include "BKE_context.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BKE_screen.hh"
#include "BLI_listbase.hh"
#include "BLI_rect.hh"

#include "DNA_screen_types.h"
#include "DNA_windowmanager_types.h"

#include "WM_api.hh"

#include <algorithm>
#include <memory>
#include <string>

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

/**
 * Blender's AppKit menubar has no Edit → Cut/Copy/Paste items, so WKWebView
 * never receives the usual Cmd-key equivalents. Route them while the WebView
 * (or one of its private content views) is first responder.
 */
@interface AgentWKWebView : WKWebView
@end

/* NSResponder editing actions (declared so the ObjC compiler sees them). */
@interface NSResponder (AgentWKWebViewEditing)
- (void)copy:(id)sender;
- (void)paste:(id)sender;
- (void)cut:(id)sender;
- (void)selectAll:(id)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
@end

@implementation AgentWKWebView

- (BOOL)agent_isEditingTarget
{
  NSResponder *fr = self.window.firstResponder;
  if (fr == nil) {
    return NO;
  }
  if (fr == self) {
    return YES;
  }
  if ([fr isKindOfClass:[NSView class]]) {
    return [(NSView *)fr isDescendantOf:self];
  }
  return NO;
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
  if (event.type != NSEventTypeKeyDown || ![self agent_isEditingTarget]) {
    return [super performKeyEquivalent:event];
  }

  const NSEventModifierFlags mods = event.modifierFlags &
                                   NSEventModifierFlagDeviceIndependentFlagsMask;
  const BOOL cmd = (mods & NSEventModifierFlagCommand) != 0;
  const BOOL ctrl = (mods & NSEventModifierFlagControl) != 0;
  const BOOL shift = (mods & NSEventModifierFlagShift) != 0;
  const BOOL alt = (mods & NSEventModifierFlagOption) != 0;
  /* macOS edit chords use Command; accept Control too (users often say Ctrl). */
  if (alt || !(cmd || ctrl)) {
    return [super performKeyEquivalent:event];
  }

  NSString *chars = event.charactersIgnoringModifiers.lowercaseString;
  if (chars.length != 1) {
    return [super performKeyEquivalent:event];
  }

  switch ([chars characterAtIndex:0]) {
    case 'c':
      [self copy:nil];
      return YES;
    case 'v':
      [self paste:nil];
      return YES;
    case 'x':
      [self cut:nil];
      return YES;
    case 'a':
      [self selectAll:nil];
      return YES;
    case 'z':
      if (shift) {
        [self redo:nil];
      }
      else {
        [self undo:nil];
      }
      return YES;
    default:
      break;
  }
  return [super performKeyEquivalent:event];
}

- (void)mouseDown:(NSEvent *)event
{
  /* Ensure subsequent Cmd-chords hit this view's responder chain. */
  [self.window makeFirstResponder:self];
  [super mouseDown:event];
}

@end

namespace blender::ed::agent {

/**
 * Native WKWebView overlay (interim until CEF OSR is linked).
 *
 * Blender menus/popovers are painted into the Metal view after region blit, so a
 * sibling NSView above Metal would cover them. While any temporary screen region
 * (menu / popup / tooltip) is visible, the WebView is hidden so those overlays
 * win. Production path: CEF windowless OnPaint → GPU texture inside the region.
 */
class CocoaWebView : public AgentWebView {
 public:
  ~CocoaWebView() override
  {
    unregister_draw_cb();
    if (web_view_ != nil) {
      /* Window may already be torn down during Blender quit. */
      if (web_view_.superview != nil) {
        [web_view_ removeFromSuperview];
      }
      [web_view_ release];
      web_view_ = nil;
    }
  }

  void load_url(const char *url) override
  {
    url_ = url ? url : "";
    if (web_view_ == nil || url_.empty()) {
      return;
    }
    NSString *ns = [NSString stringWithUTF8String:url_.c_str()];
    NSURL *nsurl = ns ? [NSURL URLWithString:ns] : nil;
    if (nsurl) {
      [web_view_ loadRequest:[NSURLRequest requestWithURL:nsurl]];
    }
  }

  void resize(int width, int height) override
  {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
  }

  void draw(const bContext *C, ARegion *region) override
  {
    wmWindow *win = CTX_wm_window(C);
    NSWindow *nswin = win ? (NSWindow *)WM_window_os_handle(win) : nil;
    if (nswin == nil || region == nullptr) {
      return;
    }
    NSView *content = nswin.contentView;
    if (content == nil) {
      return;
    }

    win_ = win;
    ensure_draw_cb(win);

    if (web_view_ == nil) {
      WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
      WKUserScript *script = [[WKUserScript alloc]
            initWithSource:[NSString stringWithUTF8String:AGENT_HOST_JS]
             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
          forMainFrameOnly:YES];
      [config.userContentController addUserScript:script];
      [script release];
      web_view_ = [[AgentWKWebView alloc] initWithFrame:NSZeroRect configuration:config];
      [config release];
      web_view_.autoresizingMask = NSViewNotSizable;
      if ([web_view_ respondsToSelector:@selector(setWantsLayer:)]) {
        web_view_.wantsLayer = YES;
      }
      /* Sit just above the GHOST Metal/OpenGL view, not above every sibling. */
      NSView *ghost_view = nil;
      for (NSView *sub in content.subviews) {
        if (sub != web_view_) {
          ghost_view = sub;
          break;
        }
      }
      if (ghost_view) {
        [content addSubview:web_view_ positioned:NSWindowAbove relativeTo:ghost_view];
      }
      else {
        [content addSubview:web_view_];
      }
      if (!url_.empty()) {
        load_url(url_.c_str());
      }
    }

    const int w = BLI_rcti_size_x(&region->winrct);
    const int h = BLI_rcti_size_y(&region->winrct);
    resize(w, h);
    const CGFloat scale = nswin.backingScaleFactor > 0.0 ? nswin.backingScaleFactor : 1.0;
    web_view_.frame = NSMakeRect(CGFloat(region->winrct.xmin) / scale,
                                 CGFloat(region->winrct.ymin) / scale,
                                 CGFloat(std::max(1, w)) / scale,
                                 CGFloat(std::max(1, h)) / scale);
    want_visible_ = (w >= 8 && h >= 8);
    apply_visibility(win);
  }

  void mouse_move(int /*x*/, int /*y*/) override {}
  void mouse_down(int /*button*/) override {}
  void mouse_up(int /*button*/) override {}
  void wheel(int /*dy*/) override {}
  void key_down(int /*key*/, bool /*ctrl*/, bool /*shift*/, bool /*alt*/) override {}
  void key_up(int /*key*/, bool /*ctrl*/, bool /*shift*/, bool /*alt*/) override {}
  void execute_js(const char *script) override
  {
    if (web_view_ && script) {
      [web_view_ evaluateJavaScript:[NSString stringWithUTF8String:script] completionHandler:nil];
    }
  }
  void set_visible(bool visible) override
  {
    want_visible_ = visible;
    apply_visibility(win_);
  }

  /** Called from #WM_draw_cb every window paint (including menu-only redraws). */
  void sync_from_window(const wmWindow *win)
  {
    if (win) {
      win_ = const_cast<wmWindow *>(win);
    }
    apply_visibility(win_);
  }

 private:
  static void window_draw_cb(const wmWindow *win, void *userdata)
  {
    static_cast<CocoaWebView *>(userdata)->sync_from_window(win);
  }

  static bool screen_has_popup_ui(const wmWindow *win)
  {
    if (win == nullptr) {
      return false;
    }
    const bScreen *screen = WM_window_get_active_screen(win);
    if (screen == nullptr) {
      return false;
    }
    /* Menus, popovers, search, tooltips live on the screen-level region list. */
    for (const ARegion &region : screen->regionbase) {
      if (region.runtime && region.runtime->visible) {
        return true;
      }
    }
    return false;
  }

  void apply_visibility(const wmWindow *win)
  {
    if (web_view_ == nil) {
      return;
    }
    const bool show = want_visible_ && !screen_has_popup_ui(win);
    web_view_.hidden = !show;
  }

  void ensure_draw_cb(wmWindow *win)
  {
    if (draw_cb_ != nullptr || win == nullptr) {
      return;
    }
    draw_cb_ = WM_draw_cb_activate(win, window_draw_cb, this);
  }

  /** True when #win_ is still owned by a live WindowManager. */
  static bool window_is_alive(const wmWindow *win)
  {
    if (win == nullptr || G_MAIN == nullptr) {
      return false;
    }
    for (wmWindowManager *wm = static_cast<wmWindowManager *>(G_MAIN->wm.first); wm != nullptr;
         wm = static_cast<wmWindowManager *>(wm->id.next))
    {
      for (const wmWindow &candidate : wm->windows) {
        if (&candidate == win) {
          return win->runtime != nullptr;
        }
      }
    }
    return false;
  }

  void unregister_draw_cb()
  {
    if (draw_cb_ == nullptr) {
      win_ = nullptr;
      return;
    }
    /* During Main teardown the wmWindow may already be gone; the drawcalls
     * list dies with it, so skipping exit only avoids a use-after-free. */
    if (window_is_alive(win_)) {
      WM_draw_cb_exit(win_, draw_cb_);
    }
    draw_cb_ = nullptr;
    win_ = nullptr;
  }

  WKWebView *web_view_ = nil;
  wmWindow *win_ = nullptr;
  void *draw_cb_ = nullptr;
  std::string url_;
  int width_ = 1;
  int height_ = 1;
  bool want_visible_ = true;
};

std::unique_ptr<AgentWebView> agent_webview_create_cocoa()
{
  return std::make_unique<CocoaWebView>();
}

}  // namespace blender::ed::agent
