add_library(level_editor
  level_editor.cc
)


target_link_libraries(level_editor
  level_panel
  parallax_panel
  parallax_preview_tab
  viewport_tab
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  gui_interface
)

add_library(level_panel
  level_panel.cc
)

target_link_libraries(level_panel
  editor_utils
  api
  level
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::cleanup
  absl::memory
  absl::strings
)

add_library(parallax_panel
  parallax_panel.cc
)

target_link_libraries(parallax_panel
  level
  api
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::cleanup
  absl::strings
)

add_library(parallax_preview_tab
  parallax_preview_tab.cc
)

target_link_libraries(parallax_preview_tab
  api
  imgui_scoped
  ${IMGUI_LIBRARIES}
  absl::status
  SDL2-static
  gui_interface
)

add_library(viewport_tab
  viewport_tab.cc
)

target_link_libraries(viewport_tab
  api
  editor_canvas
  imgui_scoped
  level
  ${IMGUI_LIBRARIES}
  absl::status
  gui_interface
)
