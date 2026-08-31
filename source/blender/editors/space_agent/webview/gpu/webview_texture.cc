/* SPDX-FileCopyrightText: 2026 Vibe3D Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "../webview.hh"

#include "BLI_rect.hh"

#include "GPU_immediate.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"

#include <algorithm>

#include "DNA_screen_types.h"

#include "ED_screen.hh"

namespace blender::ed::agent {

gpu::Texture *agent_webview_texture_ensure(gpu::Texture **slot, int width, int height)
{
  width = std::max(1, width);
  height = std::max(1, height);
  if (*slot) {
    if (GPU_texture_width(*slot) == width && GPU_texture_height(*slot) == height) {
      return *slot;
    }
    GPU_texture_free(*slot);
    *slot = nullptr;
  }
  *slot = GPU_texture_create_2d("agent_webview",
                                width,
                                height,
                                1,
                                gpu::TextureFormat::UNORM_8_8_8_8,
                                GPU_TEXTURE_USAGE_SHADER_READ,
                                nullptr);
  return *slot;
}

void agent_webview_texture_free(gpu::Texture **slot)
{
  if (slot && *slot) {
    GPU_texture_free(*slot);
    *slot = nullptr;
  }
}

void agent_webview_texture_draw(gpu::Texture *texture, const ARegion *region)
{
  if (texture == nullptr || region == nullptr) {
    return;
  }
  const int w = BLI_rcti_size_x(&region->winrct);
  const int h = BLI_rcti_size_y(&region->winrct);
  GPU_blend(GPU_BLEND_ALPHA);
  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32_32);
  uint tex = GPU_vertformat_attr_add(format, "texCoord", gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_IMAGE);
  immBindTexture("image", texture);
  immBegin(GPU_PRIM_TRI_FAN, 4);
  immAttr2f(tex, 0.0f, 0.0f);
  immVertex3f(pos, 0.0f, 0.0f, 0.0f);
  immAttr2f(tex, 1.0f, 0.0f);
  immVertex3f(pos, float(w), 0.0f, 0.0f);
  immAttr2f(tex, 1.0f, 1.0f);
  immVertex3f(pos, float(w), float(h), 0.0f);
  immAttr2f(tex, 0.0f, 1.0f);
  immVertex3f(pos, 0.0f, float(h), 0.0f);
  immEnd();
  immUnbindProgram();
  GPU_blend(GPU_BLEND_NONE);
}

}  // namespace blender::ed::agent
