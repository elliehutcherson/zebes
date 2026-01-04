add_library(level_editor
  level_editor.cc
)


target_link_libraries(level_editor
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::memory
)

add_library(level_panel
  level_panel.cc
)

target_link_libraries(level_panel
  editor_utils
  api
  level
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::cleanup
  absl::memory
  absl::strings
)
