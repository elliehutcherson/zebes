add_library(input_manager input_manager.cc)
target_link_libraries(input_manager
  common
  sdl_wrapper
  imgui_wrapper
  absl::status
  absl::statusor
  absl::flat_hash_map
  SDL2-static
)

add_library(camera_controller camera_controller.cc)
target_link_libraries(camera_controller
  input_manager
  camera
  absl::status
  absl::memory
)
