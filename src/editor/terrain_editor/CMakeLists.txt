add_library(terrain_creation terrain_creation.cc)
target_link_libraries(terrain_creation
  PUBLIC
  api
  terrain_generator
  terrain_recipe_manager
  absl::statusor
  PRIVATE
  status_macros
  terrain_detect
  absl::status
  absl::strings
)

add_library(terrain_editor_model terrain_editor_model.cc)
target_link_libraries(terrain_editor_model
  PUBLIC
  terrain_creation
  terrain_generator
  terrain_recipe
  absl::status
  PRIVATE
  status_macros
  terrain_mask
)

add_library(terrain_controls_panel terrain_controls_panel.cc)
target_link_libraries(terrain_controls_panel
  PUBLIC
  gui_interface
  terrain_editor_model
  absl::statusor
  PRIVATE
  imgui_scoped
  ${IMGUI_LIBRARIES}
  absl::memory
  absl::status
  absl::strings
)

add_library(terrain_output_panel terrain_output_panel.cc)
target_link_libraries(terrain_output_panel
  PUBLIC
  gui_interface
  terrain_editor_model
  texture
  absl::statusor
  PRIVATE
  imgui_scoped
  ${IMGUI_LIBRARIES}
  absl::memory
  absl::status
  absl::strings
)

add_library(terrain_editor terrain_editor.cc)
target_link_libraries(terrain_editor
  PUBLIC
  api
  camera
  editor_canvas
  gui_interface
  terrain_controls_panel
  terrain_creation
  terrain_editor_model
  terrain_output_panel
  terrain_recipe_manager
  absl::statusor
  PRIVATE
  imgui_scoped
  status_macros
  ${IMGUI_LIBRARIES}
  absl::cleanup
  absl::memory
  absl::status
  absl::strings
)
