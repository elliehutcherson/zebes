add_library(level_editor
  level_editor.cc
)

add_library(level_selection_state INTERFACE)
target_include_directories(level_selection_state INTERFACE .)


add_library(parallax_zone_panel
  parallax_zone_panel.cc
)

target_link_libraries(parallax_zone_panel
  level
  api
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  gui_interface
)

target_link_libraries(level_editor
  level_panel
  parallax_theme_panel
  parallax_zone_panel
  blueprint_palette_panel
  parallax_preview_tab
  viewport_tab
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  absl::flags
  gui_interface
  level_selection_state
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


add_library(parallax_theme_panel
  parallax_theme_panel.cc
)

target_link_libraries(parallax_theme_panel
  level
  api
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  absl::strings
  gui_interface
  level_selection_state
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

add_library(blueprint_palette_panel
  blueprint_palette_panel.cc
)

target_link_libraries(blueprint_palette_panel
  api
  imgui_scoped
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  absl::strings
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

