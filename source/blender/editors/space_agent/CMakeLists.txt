# SPDX-FileCopyrightText: 2026 Vibe3D Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  .
  webview
  ../include
  ../../makesrna

  # RNA_prototypes.hh
  ${CMAKE_BINARY_DIR}/source/blender/makesrna
)

set(INC_SYS
)

set(SRC
  agent_draw.cc
  agent_ops.cc
  space_agent.cc
  webview/webview.cc
  webview/gpu/webview_texture.cc

  agent_intern.hh
  webview/webview.hh
)

if(APPLE)
  list(APPEND SRC
    webview/webview_cocoa.mm
  )
endif()

if(WITH_BLENDER_CEF)
  list(APPEND SRC
    webview/cef/cef_context.cc
    webview/cef/cef_render_handler.cc
    webview/cef/cef_input.cc
  )
  add_definitions(-DWITH_BLENDER_CEF)
endif()

set(LIB
  PRIVATE bf::blenkernel
  PRIVATE bf::blenlib
  PRIVATE bf::blenloader
  PRIVATE bf::blentranslation
  PRIVATE bf::dna
  PRIVATE bf::gpu
  PRIVATE bf::intern::guardedalloc
  PRIVATE bf::windowmanager
)

if(APPLE)
  list(APPEND LIB
    "-framework AppKit"
    "-framework WebKit"
  )
endif()

blender_add_lib(bf_editor_space_agent "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# RNA_prototypes.hh dna_type_offsets.h
add_dependencies(bf_editor_space_agent bf_rna)
