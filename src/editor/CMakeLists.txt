add_executable(zebes_editor 
  main.cc
  editor_engine.cc
  editor_ui.cc
  animator.cc
)

set_target_properties(zebes_editor PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
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
  cereal::cereal
  api
  logging
  vector
  polygon
  config
  sprite
  sprite
  object
  db
)
target_compile_definitions(zebes_editor PUBLIC ABSL_MIN_LOG_LEVEL=0)
add_dependencies(zebes_editor copy_assets)