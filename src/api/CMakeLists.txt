add_library(api STATIC
  api.cc
  api.h
)

add_library(asset_root_lock asset_root_lock.cc)
target_link_libraries(asset_root_lock
  PUBLIC
  absl::statusor
  absl::time
  PRIVATE
  absl::cleanup
  absl::status
  absl::strings
)

add_library(asset_workspace asset_workspace.cc)
target_link_libraries(asset_workspace
  PUBLIC
  api
  config
  loaded_level_assets
  texture_resource_store
  PRIVATE
  asset_root_lock
  blueprint_manager
  collider_manager
  level_manager
  level_asset_loader
  parallax_artwork_recipe_manager
  parallax_theme_manager
  prop_recipe_manager
  source_artwork_manager
  sprite_manager
  terrain_recipe_manager
  texture_manager
  tileset_manager
  status_macros
  absl::memory
  absl::status
  absl::statusor
)

add_library(retained_source_transaction source_artwork_retention.cc)
target_link_libraries(retained_source_transaction
  PUBLIC
  api
  source_artwork
  absl::status
  absl::statusor
  PRIVATE
  absl::cleanup
  absl::log
  status_macros
)

target_link_libraries(api
  asset_references
  prepare_prop_asset
  prepare_parallax_artwork_asset
  regenerate_prop_asset
  regenerate_parallax_artwork_asset
  redraw_parallax_artwork_asset
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
  parallax_artwork_recipe_manager
  absl::strings
)
