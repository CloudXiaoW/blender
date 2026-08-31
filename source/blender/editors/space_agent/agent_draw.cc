/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "agent_intern.hh"

#include "BLI_rect.hh"

#include "BKE_context.hh"
#include "BKE_screen.hh"

#include "ED_screen.hh"

#include "UI_resources.hh"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_windowmanager_types.h"

#include "WM_api.hh"

namespace blender::ed::agent {

void agent_main_region_draw(const bContext *C, ARegion *region)
{
  blender::ui::theme::frame_buffer_clear(TH_BACK);
  ED_region_pixelspace(region);

  SpaceAgent *sagent = CTX_wm_space_agent(C);
  wmWindow *win = CTX_wm_window(C);
  AgentWebView *view = agent_webview_ensure(sagent, win);
  if (view) {
    view->set_visible(true);
    view->draw(C, region);
  }
}

}  // namespace blender::ed::agent
