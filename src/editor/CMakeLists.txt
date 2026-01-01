# Editor Component Libraries

# Editor Utils
add_library(editor_utils editor_utils.cc)
target_link_libraries(editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
)

# Animator
add_library(editor_animator animator.cc)
target_link_libraries(editor_animator
  absl::status
  sprite
)

# Collider Panel 
add_library(collider_panel collider_panel.cc)
target_link_libraries(collider_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  collider
)

# Sprite Panel
add_library(sprite_panel sprite_panel.cc)
target_link_libraries(sprite_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  sprite
)

# Blueprint Panel
add_library(blueprint_panel blueprint_panel.cc)
target_link_libraries(blueprint_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  blueprint_manager # Assuming blueprint object/manager availability, or api
  api
)

# Blueprint State Panel
add_library(blueprint_state_panel blueprint_state_panel.cc)
target_link_libraries(blueprint_state_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  api
)

# Canvas Elements
add_library(canvas_elements
  canvas_collider.cc
  canvas_sprite.cc
)
target_link_libraries(canvas_elements
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  collider
  sprite
)

# Canvas
add_library(editor_canvas canvas.cc)
target_link_libraries(editor_canvas
  canvas_elements
  ${IMGUI_LIBRARIES}
)

# Texture Editor
add_library(editor_texture texture_editor.cc)
target_link_libraries(editor_texture
  api
  ${IMGUI_LIBRARIES}
  absl::status
  SDL2-static
)

# Sprite Editor
add_library(editor_sprite sprite_editor.cc)
target_link_libraries(editor_sprite
  sprite_panel
  api
  ${IMGUI_LIBRARIES}
  absl::status
)

# Blueprint Editor
add_library(editor_blueprint blueprint_editor.cc)
target_link_libraries(editor_blueprint
  editor_animator
  editor_canvas
  blueprint_panel
  blueprint_state_panel
  collider_panel
  sprite_panel
  canvas_elements
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::log
  SDL2-static
)

# Config Editor
add_library(editor_config config_editor.cc)
target_link_libraries(editor_config
  api
  ${IMGUI_LIBRARIES}
  absl::status
)

# Editor UI
add_library(editor_ui editor_ui.cc)
target_link_libraries(editor_ui
  editor_blueprint
  editor_config
  editor_sprite
  editor_texture
  api
  sdl_wrapper
  absl::memory
  absl::status
  ${IMGUI_LIBRARIES}
)

# Editor Engine
add_library(editor_engine editor_engine.cc)
target_link_libraries(editor_engine
  editor_ui
  api
  blueprint_manager
  texture_manager
  sprite_manager
  collider_manager
  sdl_wrapper
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
