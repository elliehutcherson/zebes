add_library(terrain_mask
  terrain_mask.cc
)

target_link_libraries(terrain_mask
  PUBLIC
  absl::span
)

add_library(terrain_cell_key
  terrain_cell_key.cc
)

target_link_libraries(terrain_cell_key
  PUBLIC
  terrain_mask
  tileset
  absl::hash
  PRIVATE
  absl::strings
)

add_library(terrain_placement
  terrain_placement.cc
)

target_link_libraries(terrain_placement
  PUBLIC
  tileset
  absl::flat_hash_set
  absl::span
)

add_library(terrain_field
  terrain_field.cc
)

target_link_libraries(terrain_field
  PUBLIC
  absl::statusor
  PRIVATE
  absl::status
  absl::strings
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

add_library(terrain_generator
  terrain_generator.cc
)

target_link_libraries(terrain_generator
  PUBLIC
  blob47_compose
  terrain_field
  terrain_motifs
  terrain_style
  tile_shape_geometry
  tileset
  absl::statusor
  absl::span
  PRIVATE
  status_macros
  terrain_mask
  absl::status
  absl::strings
)

add_library(terrain_motifs
  terrain_motifs.cc
)

target_link_libraries(terrain_motifs
  PUBLIC
  terrain_style
  absl::status
  absl::span
  PRIVATE
  absl::strings
)

add_library(terrain_style
  terrain_style.cc
)

target_link_libraries(terrain_style
  PUBLIC
  absl::statusor
  absl::span
  PRIVATE
  absl::status
  absl::strings
)

add_library(terrain_recipe
  terrain_recipe.cc
)

target_link_libraries(terrain_recipe
  PUBLIC
  terrain_style
  absl::statusor
  nlohmann_json::nlohmann_json
  PRIVATE
  status_macros
  absl::status
  absl::strings
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
