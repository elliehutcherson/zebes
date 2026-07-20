add_library(level_editor
  level_editor.cc
)

add_library(level_selection_state INTERFACE)
target_include_directories(level_selection_state INTERFACE .)

add_library(parallax_layout
  parallax_layout.cc
)
target_link_libraries(parallax_layout
  PUBLIC level
)

add_library(camera_guide
  camera_guide.cc
)

add_library(viewport_model
  viewport_model.cc
)
target_link_libraries(viewport_model
  PUBLIC
  blueprint
  collider
  level
  sprite
  absl::status
  absl::statusor
)

add_library(viewport_scene
  viewport_scene.cc
)
target_link_libraries(viewport_scene
  PUBLIC
  camera
  entity
  level
  parallax_layout
  texture_handle
  viewport_model
  absl::flat_hash_map
  absl::status
  absl::statusor
  absl::strings
)

add_library(viewport_renderer
  viewport_renderer.cc
)
target_link_libraries(viewport_renderer
  PUBLIC
  editor_canvas
  viewport_scene
  PRIVATE
  SDL2-static
  ${IMGUI_LIBRARIES}
  absl::status
  absl::strings
)


add_library(parallax_zone_panel
  parallax_zone_panel.cc
)

target_link_libraries(parallax_zone_panel
  level
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  gui_interface
)

target_link_libraries(level_editor
  level_panel
  level_panel_model
  parallax_theme_panel
  parallax_zone_panel
  palette_panel
  parallax_layout
  viewport_tab
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  absl::flags
  gui_interface
  level_selection_state
)

add_library(level_panel_model
  level_panel_model.cc
)

target_link_libraries(level_panel_model
  PUBLIC
  level
  absl::status
  absl::statusor
)

add_library(level_panel
  level_panel.cc
)

target_link_libraries(level_panel
  level_panel_model
  level
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  gui_interface
)


add_library(parallax_theme_panel
  parallax_theme_panel.cc
)

target_link_libraries(parallax_theme_panel
  level
  api
  editor_texture_preview
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  absl::strings
  gui_interface
  level_selection_state
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

add_library(tile_palette_panel
  tile_palette_panel.cc
)

target_link_libraries(tile_palette_panel
  api
  tileset
  imgui_scoped
  ${IMGUI_LIBRARIES}
  SDL2-static
  absl::status
  absl::memory
  absl::log
  gui_interface
)

add_library(palette_panel
  palette_panel.cc
)

target_link_libraries(palette_panel
  blueprint_palette_panel
  tile_palette_panel
  api
  imgui_scoped
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  gui_interface
)

add_library(viewport_tab
  viewport_tab.cc
)

target_link_libraries(viewport_tab
  api
  camera_guide
  editor_canvas
  imgui_scoped
  level
  parallax_layout
  tileset
  viewport_model
  viewport_renderer
  viewport_scene
  ${IMGUI_LIBRARIES}
  absl::cleanup
  absl::status
  gui_interface
)
