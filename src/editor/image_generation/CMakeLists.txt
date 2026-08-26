add_library(image_generation_lifecycle_panel image_generation_lifecycle_panel.cc)
target_link_libraries(image_generation_lifecycle_panel
  PUBLIC
  gui_interface
  image_generation_request_controller
  PRIVATE
  imgui_scoped
  absl::strings
  ${IMGUI_LIBRARIES}
)
