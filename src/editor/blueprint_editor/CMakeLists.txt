# Blueprint Components

add_library(sprite_panel_model sprite_panel_model.cc)
target_link_libraries(sprite_panel_model
  PUBLIC sprite absl::status absl::statusor
)

add_library(blueprint_panel_model blueprint_panel_model.cc)
target_link_libraries(blueprint_panel_model
  PUBLIC blueprint absl::status absl::statusor
)

add_library(blueprint_panel blueprint_panel.cc)
target_link_libraries(blueprint_panel
  blueprint_panel_model
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  gui_interface
)

add_library(blueprint_state_panel blueprint_state_panel.cc)
target_link_libraries(blueprint_state_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  gui_interface
)

add_library(collider_panel_model collider_panel_model.cc)
target_link_libraries(collider_panel_model
  PUBLIC collider absl::status absl::statusor absl::strings
)

add_library(collider_panel collider_panel.cc)
target_link_libraries(collider_panel
  collider_panel_model
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  gui_interface
)

add_library(sprite_panel sprite_panel.cc)
target_link_libraries(sprite_panel
  sprite_panel_model
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  sprite
  canvas_elements
  editor_animator
  editor_canvas
  gui_interface
)

add_library(blueprint_editor blueprint_editor.cc)
target_link_libraries(blueprint_editor
  editor_animator
  editor_canvas
  blueprint_panel
  blueprint_panel_model
  blueprint_state_panel
  collider_panel
  collider_panel_model
  sprite_panel
  canvas_elements
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::log
  SDL2-static
  gui_interface
)
