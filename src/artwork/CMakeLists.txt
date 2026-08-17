add_library(prop_artwork
  cleanup_prop.cc
  compose_prop.cc
  edge_treatment.cc
  isolate_subject.cc
  prop_artwork_pipeline.cc
  quantize_prop.cc
  rasterize_prop.cc
)

target_link_libraries(prop_artwork
  PUBLIC
  image_io
  image_digest
  prop_artwork_style
  terrain_palette
  absl::span
  absl::statusor
  PRIVATE
  absl::status
  absl::strings
)

add_library(prop_artwork_style prop_artwork_style.cc)
target_link_libraries(prop_artwork_style
  PUBLIC
  terrain_palette
  absl::status
)

add_library(source_artwork source_artwork.cc)
target_link_libraries(source_artwork
  PUBLIC
  absl::status
  absl::statusor
  nlohmann_json::nlohmann_json
  PRIVATE
  status_macros
  absl::strings
)

add_library(prop_recipe prop_recipe.cc)
target_link_libraries(prop_recipe
  PUBLIC
  prop_artwork_style
  sprite
  nlohmann_json::nlohmann_json
  absl::status
  absl::statusor
  PRIVATE
  status_macros
  absl::strings
)

add_library(prepare_prop_asset prepare_prop_asset.cc)
target_link_libraries(prepare_prop_asset
  PUBLIC
  prop_artwork
  prop_recipe
  source_artwork
  blueprint
  sprite
  texture
  absl::status
  absl::statusor
  PRIVATE
  common
  image_digest
  status_macros
  absl::strings
)

add_library(regenerate_prop_asset regenerate_prop_asset.cc)
target_link_libraries(regenerate_prop_asset
  PUBLIC
  prop_artwork
  prop_artwork_style
  prop_recipe
  source_artwork
  sprite
  texture
  image_io
  absl::status
  absl::statusor
  PRIVATE
  prepare_prop_asset
  image_digest
  status_macros
  absl::strings
)
