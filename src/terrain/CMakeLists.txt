add_library(terrain_mask
  terrain_mask.cc
)

target_link_libraries(terrain_mask
  PUBLIC
  absl::span
)

add_library(terrain_detect
  terrain_detect.cc
)

target_link_libraries(terrain_detect
  PUBLIC
  blob47_compose
  terrain_mask
  tileset
  absl::statusor
  absl::strings
  PRIVATE
  status_macros
  absl::flat_hash_map
  absl::status
)

add_library(blob47_compose
  blob47_compose.cc
)

target_link_libraries(blob47_compose
  PUBLIC
  terrain_mask
  tileset
  absl::statusor
  PRIVATE
  status_macros
  absl::status
  absl::strings
)
