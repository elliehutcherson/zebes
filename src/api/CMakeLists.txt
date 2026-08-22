add_library(api STATIC
  api.cc
  api.h
)

target_link_libraries(api
  asset_references
  prepare_prop_asset
  regenerate_prop_asset
  image_digest
  absl::status
  absl::statusor
  absl::time
  common
  status_macros
  sprite
  sprite_manager
  texture_manager
  collider_manager
  blueprint_manager
  level_manager
  parallax_theme_manager
  tileset_manager
  terrain_recipe_manager
  source_artwork_manager
  prop_recipe_manager
  absl::strings
)
