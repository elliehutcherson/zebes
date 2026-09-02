add_library(resource_utils resource_utils.cc)
target_link_libraries(resource_utils
  PUBLIC absl::status
  PRIVATE absl::strings
)

add_library(texture_resource_store INTERFACE texture_resource_store.h)
target_link_libraries(texture_resource_store INTERFACE texture_handle absl::status absl::statusor)

add_library(texture_manager texture_manager.cc)
target_link_libraries(texture_manager common)
target_link_libraries(texture_manager absl::status)
target_link_libraries(texture_manager absl::statusor)
target_link_libraries(texture_manager absl::flat_hash_map)
target_link_libraries(texture_manager absl::cleanup)
target_link_libraries(texture_manager texture_resource_store)
target_link_libraries(texture_manager texture)
target_link_libraries(texture_manager resource_utils)
target_link_libraries(texture_manager image_io)

add_library(sprite_manager sprite_manager.cc)
target_link_libraries(sprite_manager common)
target_link_libraries(sprite_manager sprite)
target_link_libraries(sprite_manager texture_manager)
target_link_libraries(sprite_manager absl::status)
target_link_libraries(sprite_manager absl::statusor)
target_link_libraries(sprite_manager absl::flat_hash_map)
target_link_libraries(sprite_manager resource_utils)

add_library(collider_manager collider_manager.cc)
target_link_libraries(collider_manager common)
target_link_libraries(collider_manager absl::status)
target_link_libraries(collider_manager absl::statusor)
target_link_libraries(collider_manager absl::flat_hash_map)
target_link_libraries(collider_manager resource_utils)

add_library(blueprint_manager blueprint_manager.cc)
target_link_libraries(blueprint_manager common)
target_link_libraries(blueprint_manager absl::status)
target_link_libraries(blueprint_manager absl::statusor)
target_link_libraries(blueprint_manager absl::flat_hash_map)
target_link_libraries(blueprint_manager absl::flat_hash_set)
target_link_libraries(blueprint_manager blueprint)
target_link_libraries(blueprint_manager resource_utils)

add_library(level_manager level_manager.cc)
target_link_libraries(level_manager common)
target_link_libraries(level_manager absl::status)
target_link_libraries(level_manager absl::statusor)
target_link_libraries(level_manager absl::flat_hash_map)
target_link_libraries(level_manager level)
target_link_libraries(level_manager sprite_manager)
target_link_libraries(level_manager collider_manager)
target_link_libraries(level_manager resource_utils)

add_library(parallax_theme_manager parallax_theme_manager.cc)
target_link_libraries(parallax_theme_manager
  PUBLIC
  parallax_theme
  absl::status
  absl::statusor
  PRIVATE
  common
  resource_utils
  status_macros
  absl::flat_hash_map
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(tileset_manager tileset_manager.cc)
target_link_libraries(tileset_manager common)
target_link_libraries(tileset_manager absl::status)
target_link_libraries(tileset_manager absl::statusor)
target_link_libraries(tileset_manager absl::flat_hash_map)
target_link_libraries(tileset_manager absl::flat_hash_set)
target_link_libraries(tileset_manager tileset)
target_link_libraries(tileset_manager resource_utils)

add_library(terrain_recipe_manager terrain_recipe_manager.cc)
target_link_libraries(terrain_recipe_manager
  PUBLIC
  terrain_recipe
  prop_recipe
  parallax_artwork_recipe
  absl::status
  absl::statusor
  PRIVATE
  common
  resource_utils
  status_macros
  absl::flat_hash_map
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(source_artwork_manager source_artwork_manager.cc)
target_link_libraries(source_artwork_manager
  PUBLIC
  source_artwork
  image_io
  absl::status
  absl::statusor
  PRIVATE
  common
  image_digest
  resource_utils
  status_macros
  absl::flat_hash_map
  absl::cleanup
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(prop_recipe_manager prop_recipe_manager.cc)
target_link_libraries(prop_recipe_manager
  PUBLIC
  prop_recipe
  absl::status
  absl::statusor
  PRIVATE
  common
  resource_utils
  status_macros
  absl::flat_hash_map
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(parallax_artwork_recipe_manager parallax_artwork_recipe_manager.cc)
target_link_libraries(parallax_artwork_recipe_manager
  PUBLIC
  parallax_artwork_recipe
  absl::status
  absl::statusor
  PRIVATE
  common
  resource_utils
  status_macros
  absl::flat_hash_map
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(animation_frame_set_recipe_manager animation_frame_set_recipe_manager.cc)
target_link_libraries(animation_frame_set_recipe_manager
  PUBLIC
  animation_frame_set_recipe
  absl::status
  absl::statusor
  PRIVATE
  common
  resource_utils
  status_macros
  absl::flat_hash_map
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(asset_references asset_references.cc)
target_link_libraries(asset_references
  PUBLIC
  blueprint
  level
  parallax_theme
  sprite
  tileset
  terrain_recipe
  prop_recipe
  animation_frame_set_recipe
  PRIVATE
  absl::strings
)

add_library(loaded_level_assets INTERFACE loaded_level_assets.h)
target_link_libraries(loaded_level_assets
  INTERFACE
  blueprint
  collider
  level
  parallax_theme
  sprite
  texture_handle
  tileset
  absl::flat_hash_map
)

add_library(level_asset_loader level_asset_loader.cc)
target_link_libraries(level_asset_loader
  PUBLIC
  loaded_level_assets
  blueprint_manager
  collider_manager
  level_manager
  parallax_theme_manager
  sprite_manager
  texture_manager
  tileset_manager
  absl::statusor
  PRIVATE
  status_macros
  absl::flat_hash_set
  absl::status
  absl::strings
)
