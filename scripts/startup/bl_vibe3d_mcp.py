# SPDX-FileCopyrightText: Vibe3D
# SPDX-License-Identifier: GPL-2.0-or-later
"""Enable the bundled blender_mcp_addon (addons_core) for existing user prefs."""

from __future__ import annotations

import addon_utils
import bpy
from bpy.app.handlers import persistent


@persistent
def _ensure_mcp(*_args) -> None:
    if "blender_mcp_addon" in bpy.context.preferences.addons:
        return
    try:
        addon_utils.enable("blender_mcp_addon", default_set=True, persistent=True)
    except Exception:
        pass


def register():
    _ensure_mcp()
    if _ensure_mcp not in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.append(_ensure_mcp)


def unregister():
    if _ensure_mcp in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(_ensure_mcp)
