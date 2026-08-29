add_library(input_types INTERFACE input_types.h)

add_library(texture_handle INTERFACE texture_handle.h)

add_library(scene_types scene_types.cc)
target_link_libraries(scene_types
  PUBLIC
  camera
  entity
  sprite
  texture_handle
  absl::flat_hash_map
  absl::status
  absl::statusor
)

add_library(parallax_layout parallax_layout.cc)
target_link_libraries(parallax_layout
  PUBLIC
  parallax_theme
  scene_types
  absl::statusor
  PRIVATE
  status_macros
  absl::status
  absl::strings
)

add_library(scene_composition scene_composition.cc)
target_link_libraries(scene_composition
  PUBLIC
  camera
  entity
  level
  parallax_theme
  scene_types
  texture_handle
  tileset
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  status_macros
  absl::status
  absl::strings
)

add_library(animation animation.cc)
target_link_libraries(animation
  PUBLIC
  sprite
  absl::statusor
  PRIVATE
  absl::status
  status_macros
)

add_library(collision collision.cc)
target_link_libraries(collision
  PUBLIC
  vec
  absl::span
  absl::statusor
  PRIVATE
  status_macros
  absl::status
)

add_library(tile_collision tile_collision.cc)
target_link_libraries(tile_collision
  PUBLIC
  collision
  tile_shape_geometry
  tileset
  vec
  absl::statusor
  PRIVATE
  status_macros
  absl::status
)

add_library(tile_movement tile_movement.cc)
target_link_libraries(tile_movement
  PUBLIC
  level
  tile_collision
  tileset
  vec
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  status_macros
  tile_shape_geometry
  absl::status
  absl::strings
)

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
