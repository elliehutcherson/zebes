# Blueprint Components

add_library(blueprint_panel blueprint_panel.cc)
target_link_libraries(blueprint_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  blueprint_manager
  api
)

add_library(blueprint_state_panel blueprint_state_panel.cc)
target_link_libraries(blueprint_state_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  api
)

add_library(collider_panel collider_panel.cc)
target_link_libraries(collider_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  collider
)

add_library(sprite_panel sprite_panel.cc)
target_link_libraries(sprite_panel
  editor_utils
  ${IMGUI_LIBRARIES}
  absl::status
  sprite
)

add_library(blueprint_editor blueprint_editor.cc)
target_link_libraries(blueprint_editor
  editor_animator
  editor_canvas
  blueprint_panel
  blueprint_state_panel
  collider_panel
  sprite_panel
  canvas_elements
  api
  ${IMGUI_LIBRARIES}
  absl::status
  absl::log
  SDL2-static
)
