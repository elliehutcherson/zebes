add_executable(zebes_editor
  main.cc
  editor_engine.cc
  editor_ui.cc
  editor_engine.cc
  animator.cc
  texture_editor.cc
  config_editor.cc
  sprite_editor.cc
)

target_link_libraries(zebes_editor
  absl::base
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
  absl::status
  absl::strings
  absl::str_format
  SDL2main
  SDL2-static
  SDL2_image
  SDL2_ttf
  ${IMGUI_LIBRARIES}
  ${SQLITE3_LIBRARIES}
  nlohmann_json::nlohmann_json
  api
  logging
  config
  sprite
  texture_manager
  sprite_manager
  sdl_wrapper
)
target_compile_definitions(zebes_editor PUBLIC ABSL_MIN_LOG_LEVEL=0)
add_dependencies(zebes_editor copy_assets)
