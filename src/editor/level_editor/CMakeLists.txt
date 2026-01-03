add_library(level_editor
  level_editor.cc
)

target_link_libraries(level_editor
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
)
