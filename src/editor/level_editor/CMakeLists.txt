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

add_library(level_tiles
  level_tiles.cc
)
target_link_libraries(level_tiles
  PUBLIC level
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
  tileset
  absl::status
  absl::statusor
)

add_library(derived_terrain_session
  derived_terrain_session.cc
)

target_link_libraries(derived_terrain_session
  PUBLIC
  api
  derived_tile_provider
  tileset
  absl::status
  PRIVATE
  status_macros
  absl::strings
)

add_library(derived_tile_provider
  derived_tile_provider.cc
)

target_link_libraries(derived_tile_provider
  PUBLIC
  image_io
  terrain_brush
  terrain_cell_key
  terrain_content_index
  terrain_generator
  tileset
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  status_macros
  absl::strings
)

add_library(terrain_brush
  terrain_brush.cc
)

target_link_libraries(terrain_brush
  PUBLIC
  level
  terrain_cell_key
  tileset
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  status_macros
  terrain_mask
  viewport_model
  absl::status
  absl::strings
)

add_library(viewport_interaction
  viewport_interaction.cc
)

target_link_libraries(viewport_interaction
  PUBLIC
  blueprint
  entity
  level
  sprite
  terrain_brush
  viewport_model
  absl::statusor
  PRIVATE
  status_macros
  absl::status
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
  derived_terrain_session
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
  PRIVATE
  level_tiles
)

add_library(level_panel
  level_panel.cc
)

target_link_libraries(level_panel
  level_panel_model
  level
  confirm_prompt
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
  absl::strings
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
  editor_texture_preview
  absl::status
  absl::memory
  absl::log
  gui_interface
)

add_library(terrain_palette_panel
  terrain_palette_panel.cc
)

target_link_libraries(terrain_palette_panel
  api
  tileset
  terrain_placement
  imgui_scoped
  ${IMGUI_LIBRARIES}
  editor_texture_preview
  absl::status
  absl::memory
  absl::strings
  gui_interface
)

add_library(palette_panel
  palette_panel.cc
)

target_link_libraries(palette_panel
  blueprint_palette_panel
  tile_palette_panel
  terrain_palette_panel
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
  viewport_interaction
  viewport_model
  viewport_renderer
  viewport_scene
  ${IMGUI_LIBRARIES}
  absl::cleanup
  absl::status
  gui_interface
)
