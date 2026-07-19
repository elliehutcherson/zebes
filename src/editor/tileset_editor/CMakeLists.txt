add_library(tileset_editor_model tileset_editor_model.cc)
target_link_libraries(tileset_editor_model
  PUBLIC
  absl::status
  absl::statusor
  texture
  tileset
)

add_library(tile_panel tile_panel.cc)
target_link_libraries(tile_panel
  gui_interface
  imgui_scoped
  ${IMGUI_LIBRARIES}
  absl::strings
)

add_library(tileset_panel tileset_panel.cc)
target_link_libraries(tileset_panel
  tileset_editor_model
  gui_interface
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::statusor
  absl::memory
  absl::strings
)

add_library(tileset_editor tileset_editor.cc)
target_link_libraries(tileset_editor
  tile_panel
  tileset_panel
  tileset_editor_model
  api
  editor_canvas
  gui_interface
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  SDL2-static
  absl::log
  absl::status
  absl::memory
)
