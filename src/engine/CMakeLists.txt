add_library(input_types INTERFACE input_types.h)

add_library(texture_handle INTERFACE texture_handle.h)

add_library(input_manager input_manager.cc)
target_link_libraries(input_manager
  PUBLIC input_types
  PRIVATE
  absl::status
  absl::statusor
  absl::flat_hash_map
)

add_library(camera_controller camera_controller.cc)
target_link_libraries(camera_controller
  input_manager
  camera
  absl::status
  absl::memory
)
