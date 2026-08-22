add_library(parallax_theme_editor_model parallax_theme_editor_model.cc)
target_link_libraries(parallax_theme_editor_model
  PUBLIC parallax_theme absl::status absl::statusor
  PRIVATE absl::strings
)

add_library(parallax_diagnostics parallax_diagnostics.cc)
target_link_libraries(parallax_diagnostics
  PUBLIC image_io camera game_view parallax_theme absl::statusor
  PRIVATE absl::status
)

add_library(parallax_theme_editor parallax_theme_editor.cc)
target_link_libraries(parallax_theme_editor
  PUBLIC api gui_interface parallax_theme_editor_model
  PRIVATE
  confirm_prompt
  editor_canvas
  editor_texture_preview
  imgui_scoped
  parallax_diagnostics
  status_macros
  viewport_renderer
  viewport_scene
  absl::memory
  absl::strings
  absl::status
  absl::statusor
  absl::strings
  ${IMGUI_LIBRARIES}
)
