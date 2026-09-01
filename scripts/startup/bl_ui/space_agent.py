# SPDX-FileCopyrightText: 2026 Vibe3D Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Agent editor: header chrome around the embedded conversation WebView."""

import bpy
from bpy.types import Header, Menu, Operator


class AGENT_HT_header(Header):
    bl_space_type = 'AGENT'

    def draw(self, context):
        layout = self.layout
        layout.template_header()
        layout.label(text="Agent")
        AGENT_MT_editor_menus.draw_collapsible(context, layout)
        layout.separator_spacer()
        space = context.space_data
        if space is not None and hasattr(space, "url"):
            layout.prop(space, "url", text="")
        layout.operator("agent.reload", text="", icon='FILE_REFRESH')


class AGENT_MT_editor_menus(Menu):
    bl_idname = "AGENT_MT_editor_menus"
    bl_label = ""

    def draw(self, _context):
        layout = self.layout
        layout.menu("INFO_MT_area")


def _window_region(area):
    for candidate in area.regions:
        if candidate.type == 'WINDOW':
            return candidate
    return None


class SCREEN_OT_agent_new(Operator):
    """Split the 3D Viewport and open the Agent conversation on the right."""

    bl_idname = "screen.agent_new"
    bl_label = "New Agent"
    bl_description = "Open the Agent editor, or replace the current Agent with a new conversation"
    bl_options = {'REGISTER'}

    def execute(self, context):
        screen = context.screen
        if screen is None:
            return {'CANCELLED'}

        for area in screen.areas:
            if area.type != 'AGENT':
                continue
            region = _window_region(area)
            with context.temp_override(
                window=context.window,
                screen=screen,
                area=area,
                region=region,
            ):
                try:
                    bpy.ops.agent.reload(new_session=True)
                except TypeError:
                    bpy.ops.agent.reload()
            return {'FINISHED'}

        view3d = None
        for area in screen.areas:
            if area.type == 'VIEW_3D':
                view3d = area
                break
        if view3d is None:
            if context.area is not None:
                context.area.type = 'AGENT'
                return {'FINISHED'}
            return {'CANCELLED'}

        region = None
        for candidate in view3d.regions:
            if candidate.type == 'WINDOW':
                region = candidate
                break
        try:
            with context.temp_override(
                window=context.window,
                screen=screen,
                area=view3d,
                region=region,
            ):
                bpy.ops.screen.area_split(direction='VERTICAL', factor=0.70)
        except Exception:
            view3d.type = 'AGENT'
            return {'FINISHED'}

        right = None
        for area in screen.areas:
            if area == view3d:
                continue
            if area.x >= view3d.x + view3d.width - 4 and abs(area.y - view3d.y) < 8:
                if right is None or area.x < right.x:
                    right = area
        if right is None:
            view3d.type = 'AGENT'
            return {'FINISHED'}

        right.type = 'AGENT'
        return {'FINISHED'}


classes = (
    AGENT_HT_header,
    AGENT_MT_editor_menus,
    SCREEN_OT_agent_new,
)


def register():
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)


def unregister():
    from bpy.utils import unregister_class
    for cls in reversed(classes):
        unregister_class(cls)
