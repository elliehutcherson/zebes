# Editor Component Libraries

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

# Animator
add_library(editor_animator animator.cc)
target_link_libraries(editor_animator
  absl::status
  sprite
)

# Canvas Elements
add_library(canvas_elements
  canvas/canvas_collider.cc
  canvas/canvas_sprite.cc
)
target_link_libraries(canvas_elements
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
)

# Texture Editor
add_library(editor_texture texture_editor/texture_editor.cc)
target_link_libraries(editor_texture
  gui_interface
  api
  ${IMGUI_LIBRARIES}
  absl::status
  SDL2-static
)

# Blueprint Components
add_subdirectory(blueprint_editor)

# Sprite Editor
add_library(editor_sprite sprite_editor/sprite_editor.cc)
target_link_libraries(editor_sprite
  sprite_panel
  api
  ${IMGUI_LIBRARIES}
  absl::status
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


# Editor UI
add_library(editor_ui editor_ui.cc)
target_link_libraries(editor_ui
  blueprint_editor
  editor_gui
  editor_config
  editor_sprite
  editor_texture
  level_editor
  api
  sdl_wrapper
  absl::memory
  absl::status
  gui_interface
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
