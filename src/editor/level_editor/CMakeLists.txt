add_library(level_editor
  level_editor.cc
)


target_link_libraries(level_editor
  level_panel
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

add_library(parallax_panel
  parallax_panel.cc
)

target_link_libraries(parallax_panel
  level
  status_macros
  ${IMGUI_LIBRARIES}
  absl::status
  absl::cleanup
  absl::strings
)
