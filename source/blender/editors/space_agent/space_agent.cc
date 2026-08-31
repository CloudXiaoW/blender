/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spagent
 */

#include "MEM_guardedalloc.h"

#include "BLI_listbase.hh"
#include "BLI_string.hh"

#include <memory>

#include "BKE_context.hh"
#include "BKE_screen.hh"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_windowmanager_types.h"

#include "ED_screen.hh"
#include "ED_space_api.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "UI_resources.hh"

#include "BLO_read_write.hh"

#include "agent_intern.hh"

namespace blender {

using ed::agent::agent_default_url;
using ed::agent::agent_main_region_draw;
using ed::agent::agent_webview_free;
using ed::agent::agent_webview_get;

static SpaceLink *agent_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  SpaceAgent *sagent = MEM_new<SpaceAgent>("initagent");
  sagent->spacetype = SPACE_AGENT;
  agent_default_url(sagent->url, SPACE_AGENT_URL_MAX);

  {
    ARegion *region = BKE_area_region_new();
    BLI_addtail(&sagent->regionbase, region);
    region->regiontype = RGN_TYPE_HEADER;
    region->alignment = RGN_ALIGN_TOP;
  }

  {
    ARegion *region = BKE_area_region_new();
    BLI_addtail(&sagent->regionbase, region);
    region->regiontype = RGN_TYPE_WINDOW;
  }

  return reinterpret_cast<SpaceLink *>(sagent);
}

static void agent_free(SpaceLink *sl)
{
  agent_webview_free(reinterpret_cast<SpaceAgent *>(sl));
}

static void agent_init(wmWindowManager * /*wm*/, ScrArea * /*area*/) {}

static void agent_exit(wmWindowManager * /*wm*/, ScrArea *area)
{
  if (area == nullptr || area->spacetype != SPACE_AGENT) {
    return;
  }
  /* Tear down the native WebView while wmWindow is still alive. Waiting until
   * SpaceLink free (during Main teardown) leaves a dangling draw-callback
   * window pointer and crashes on quit. */
  SpaceAgent *sagent = static_cast<SpaceAgent *>(area->spacedata.first);
  agent_webview_free(sagent);
}

static SpaceLink *agent_duplicate(SpaceLink *sl)
{
  SpaceAgent *sagentn = MEM_dupalloc(reinterpret_cast<SpaceAgent *>(sl));
  return reinterpret_cast<SpaceLink *>(sagentn);
}

static void agent_operatortypes()
{
  WM_operatortype_append(AGENT_OT_reload);
}

static void agent_keymap(wmKeyConfig *keyconf)
{
  WM_keymap_ensure(keyconf, "Agent", SPACE_AGENT, RGN_TYPE_WINDOW);
}

static void agent_space_blend_write(BlendWriter *writer, SpaceLink *sl)
{
  writer->write_struct_cast<SpaceAgent>(sl);
}

static void agent_main_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap = WM_keymap_ensure(
      wm->runtime->defaultconf, "Agent", SPACE_AGENT, RGN_TYPE_WINDOW);
  WM_event_add_keymap_handler(&region->runtime->handlers, keymap);
}

static void agent_header_region_init(wmWindowManager * /*wm*/, ARegion *region)
{
  ED_region_header_init(region);
}

static void agent_header_region_draw(const bContext *C, ARegion *region)
{
  ED_region_header(C, region);
}

void ED_spacetype_agent()
{
  std::unique_ptr<SpaceType> st = std::make_unique<SpaceType>();
  ARegionType *art;

  st->spaceid = SPACE_AGENT;
  STRNCPY(st->name, "Agent");
  st->iconid = ICON_INFO;

  st->create = agent_create;
  st->free = agent_free;
  st->init = agent_init;
  st->exit = agent_exit;
  st->duplicate = agent_duplicate;
  st->operatortypes = agent_operatortypes;
  st->keymap = agent_keymap;
  st->blend_write = agent_space_blend_write;

  art = MEM_new_zeroed<ARegionType>("spacetype agent region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->init = agent_main_region_init;
  art->draw = agent_main_region_draw;
  BLI_addhead(&st->regiontypes, art);

  art = MEM_new_zeroed<ARegionType>("spacetype agent region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
  art->init = agent_header_region_init;
  art->draw = agent_header_region_draw;
  BLI_addhead(&st->regiontypes, art);

  BKE_spacetype_register(std::move(st));
}

}  // namespace blender
