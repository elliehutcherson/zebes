add_executable(zebes_editor 
  main.cc
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
)
target_compile_definitions(zebes_engine PUBLIC ABSL_MIN_LOG_LEVEL=0)
add_dependencies(zebes_engine copy_assets)