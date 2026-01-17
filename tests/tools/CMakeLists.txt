add_executable(panel_viewer panel_viewer.cc)
target_link_libraries(panel_viewer
  level_panel
  parallax_panel
  editor_gui
  api
  gmock
  ${IMGUI_LIBRARIES}
  SDL2-static
  absl::flags
  absl::flags_parse
  absl::log
  absl::check
  absl::status
  absl::statusor
)
target_include_directories(panel_viewer PRIVATE ${CMAKE_SOURCE_DIR})
