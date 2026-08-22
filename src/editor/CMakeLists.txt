# Editor Component Libraries

# Gui Interface
add_library(gui_interface INTERFACE)
target_link_libraries(gui_interface INTERFACE
  absl::status
  ${IMGUI_LIBRARIES}
)


# ImGui Scoped (and GuiInterface)
add_library(imgui_scoped imgui_scoped.cc)
target_link_libraries(imgui_scoped
  gui_interface
  ${IMGUI_LIBRARIES}
)


# Inline confirmation for destructive buttons, shared by every panel that has
# one so the interaction cannot drift between tabs.
add_library(confirm_prompt confirm_prompt.cc)
target_link_libraries(confirm_prompt
  gui_interface
  imgui_scoped
  absl::strings
  ${IMGUI_LIBRARIES}
)


# Gui Implementation
add_library(editor_gui gui.cc)
target_link_libraries(editor_gui
  imgui_scoped
  ${IMGUI_LIBRARIES}
)

# Editor Utils
add_library(editor_utils editor_utils.cc)
target_link_libraries(editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
)

# Shared, platform-neutral editor gizmo geometry and its ImGui adapter.
add_library(anchor_gizmo anchor_gizmo.cc)
target_link_libraries(anchor_gizmo
  PUBLIC
  blueprint
  absl::statusor
  PRIVATE
  absl::status
)

add_library(anchor_gizmo_renderer anchor_gizmo_renderer.cc)
target_link_libraries(anchor_gizmo_renderer
  PUBLIC
  anchor_gizmo
  editor_canvas
  PRIVATE
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
)

# Shared visual-palette layout and selection chrome.
add_library(palette_ui palette_ui.cc)
target_link_libraries(palette_ui
  PUBLIC
  absl::statusor
  ${IMGUI_LIBRARIES}
  PRIVATE
  absl::status
)

# Animator
add_library(editor_animator animator.cc)
target_link_libraries(editor_animator
  absl::status
  absl::statusor
  sprite
  status_macros
)

# Shared managed-texture preview adapter
add_library(editor_texture_preview texture_preview.cc)
target_link_libraries(editor_texture_preview
  PUBLIC
  camera
  gui_interface
  texture_handle
  absl::statusor
  PRIVATE
  SDL2-static
  absl::status
  absl::strings
  status_macros
)

# Canvas Elements
add_library(canvas_elements
  canvas/canvas_collider.cc
  canvas/canvas_sprite.cc
)
target_link_libraries(canvas_elements
  editor_animator
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  collider
  sprite
)

# Canvas
add_library(editor_canvas canvas/canvas.cc)
target_link_libraries(editor_canvas
  canvas_elements
  ${IMGUI_LIBRARIES}
  gui_interface
  tile_shape_geometry
)

# Texture Editor
add_library(texture_editor_model
  texture_editor/texture_editor_model.cc
)
target_link_libraries(texture_editor_model
  PUBLIC texture absl::status absl::statusor
)

add_library(editor_texture texture_editor/texture_editor.cc)
target_link_libraries(editor_texture
  texture_editor_model
  confirm_prompt
  gui_interface
  api
  ${IMGUI_LIBRARIES}
  absl::status
  SDL2-static
)

# Blueprint Components
add_subdirectory(blueprint_editor)
add_subdirectory(image_generation)

# Sprite Editor
add_library(sprite_editor_model sprite_editor/sprite_editor_model.cc)
target_link_libraries(sprite_editor_model
  PUBLIC sprite texture absl::status absl::statusor absl::strings)

add_library(editor_sprite sprite_editor/sprite_editor.cc)
target_link_libraries(editor_sprite
  anchor_gizmo_renderer
  editor_animator
  sprite_editor_model
  sprite_panel
  confirm_prompt
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::strings
)



# Config Editor
add_library(editor_config config_editor/config_editor.cc)
target_link_libraries(editor_config
  gui_interface
  api
  ${IMGUI_LIBRARIES}
  absl::status
)

# Level Editor
add_subdirectory(level_editor)

# Tileset Editor
add_subdirectory(tileset_editor)
add_subdirectory(terrain_editor)
add_subdirectory(prop_artwork_editor)


# Editor UI
add_library(sdl_preview_texture sdl_preview_texture.cc)
target_link_libraries(sdl_preview_texture
  gui_interface
  blob47_compose
  sdl_wrapper
  status_macros
  SDL2-static
  ${IMGUI_LIBRARIES}
  absl::status
  absl::statusor
)

add_library(editor_ui editor_ui.cc)
target_link_libraries(editor_ui
  sdl_preview_texture
  blueprint_editor
  editor_config
  editor_sprite
  editor_texture
  image_generation_service
  level_editor
  tileset_editor
  terrain_editor
  prop_artwork_editor
  api
  gui_interface
  imgui_scoped
  sdl_wrapper
  absl::log
  absl::memory
  absl::status
  absl::statusor
  ${IMGUI_LIBRARIES}
)

# Editor Engine
add_library(editor_engine editor_engine.cc)
target_link_libraries(editor_engine
  editor_ui
  editor_gui
  api
  blueprint_manager
  texture_manager
  sprite_manager
  collider_manager
  level_manager
  sdl_wrapper
  imgui_wrapper
  input_manager
  sdl_input_source
  sdl_texture_store
  config
  ${IMGUI_LIBRARIES}
  ${SQLITE3_LIBRARIES}
  nlohmann_json::nlohmann_json
  SDL2-static
  SDL2_image
  SDL2_ttf
  absl::flags
  absl::log
  absl::strings
  absl::status
)
target_compile_definitions(editor_engine PUBLIC ABSL_MIN_LOG_LEVEL=0)

# Main Executable
add_executable(zebes_editor main.cc)
target_link_libraries(zebes_editor
  editor_engine
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
)
add_dependencies(zebes_editor copy_assets)
