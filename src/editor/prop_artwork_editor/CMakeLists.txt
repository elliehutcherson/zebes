add_library(prop_artwork_context prop_artwork_context.cc)
target_link_libraries(prop_artwork_context
  PUBLIC
  prop_artwork
  image_io
  terrain_style
  absl::statusor
  PRIVATE
  terrain_generator
  status_macros
  absl::status
)

add_library(prop_artwork_editor_model prop_artwork_editor_model.cc)
target_link_libraries(prop_artwork_editor_model
  PUBLIC
  image_generation
  prepare_prop_asset
  prop_artwork_context
  regenerate_prop_asset
  source_artwork
  terrain_recipe
  absl::status
  PRIVATE
  image_digest
  terrain_palette
  status_macros
)

add_library(prop_artwork_controls_panel prop_artwork_controls_panel.cc)
target_link_libraries(prop_artwork_controls_panel
  PUBLIC
  confirm_prompt
  gui_interface
  image_generation
  prop_artwork_editor_model
  source_artwork
  terrain_recipe
  absl::statusor
  PRIVATE
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::memory
  absl::status
)

add_library(prop_artwork_output_panel prop_artwork_output_panel.cc)
target_link_libraries(prop_artwork_output_panel
  PUBLIC
  confirm_prompt
  gui_interface
  prop_artwork_editor_model
  prop_recipe
  absl::statusor
  PRIVATE
  imgui_scoped
  ${IMGUI_LIBRARIES}
  absl::memory
  absl::status
  absl::strings
)

add_library(prop_artwork_editor prop_artwork_editor.cc)
target_link_libraries(prop_artwork_editor
  PUBLIC
  anchor_gizmo_renderer
  api
  background_task
  camera
  editor_canvas
  gui_interface
  image_generation
  image_generation_request_controller
  prop_artwork_controls_panel
  prop_artwork_context
  prop_artwork_editor_model
  prop_artwork_output_panel
  absl::status
  absl::statusor
  PRIVATE
  editor_texture_preview
  imgui_scoped
  image_io
  status_macros
  ${IMGUI_LIBRARIES}
  absl::cleanup
  absl::memory
  absl::strings
  absl::time
)
