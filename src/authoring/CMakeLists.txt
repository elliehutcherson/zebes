add_library(environment_builder environment_builder.cc)
target_link_libraries(environment_builder
  PUBLIC
  api
  level
  parallax_artwork_recipe
  parallax_theme
  tileset
  absl::status
  absl::statusor
  PRIVATE
  derived_terrain_session
  entity_factory
  terrain_brush
  status_macros
  absl::flat_hash_set
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(terrain_builder terrain_builder.cc)
target_link_libraries(terrain_builder
  PUBLIC
  api
  terrain_creation
  terrain_recipe
  terrain_style
  absl::status
  absl::statusor
  PRIVATE
  status_macros
  absl::strings
  nlohmann_json::nlohmann_json
)
