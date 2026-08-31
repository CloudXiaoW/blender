/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spagent
 */

#include "BKE_context.hh"

#include "ED_screen.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "agent_intern.hh"

namespace blender {

static bool agent_poll(bContext *C)
{
  return CTX_wm_space_agent(C) != nullptr;
}

static wmOperatorStatus agent_reload_exec(bContext *C, wmOperator * /*op*/)
{
  SpaceAgent *sagent = CTX_wm_space_agent(C);
  if (sagent == nullptr) {
    return OPERATOR_CANCELLED;
  }
  /* Re-resolve so a freshly written dsh launch token is picked up after auth. */
  ed::agent::agent_default_url(sagent->url, SPACE_AGENT_URL_MAX);
  if (ed::agent::AgentWebView *view = ed::agent::agent_webview_ensure(sagent, CTX_wm_window(C))) {
    view->load_url(sagent->url);
  }
  ED_area_tag_redraw(CTX_wm_area(C));
  return OPERATOR_FINISHED;
}

void AGENT_OT_reload(wmOperatorType *ot)
{
  ot->name = "Reload Agent";
  ot->idname = "AGENT_OT_reload";
  ot->description = "Reload the Agent conversation page";
  ot->exec = agent_reload_exec;
  ot->poll = agent_poll;
}

}  // namespace blender
