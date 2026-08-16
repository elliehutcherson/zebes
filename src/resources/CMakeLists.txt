add_library(resource_utils resource_utils.cc)
target_link_libraries(resource_utils absl::strings)

add_library(texture_resource_store INTERFACE texture_resource_store.h)
target_link_libraries(texture_resource_store INTERFACE texture_handle absl::status absl::statusor)

add_library(texture_manager texture_manager.cc)
target_link_libraries(texture_manager common)
target_link_libraries(texture_manager absl::status)
target_link_libraries(texture_manager absl::statusor)
target_link_libraries(texture_manager absl::flat_hash_map)
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
  absl::status
  absl::statusor
  PRIVATE
  common
  status_macros
  absl::flat_hash_map
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(source_artwork_manager source_artwork_manager.cc)
target_link_libraries(source_artwork_manager
  PUBLIC
  source_artwork
  prop_artwork
  image_io
  absl::status
  absl::statusor
  PRIVATE
  common
  image_digest
  resource_utils
  status_macros
  absl::flat_hash_map
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

add_library(asset_references asset_references.cc)
target_link_libraries(asset_references
  PUBLIC
  blueprint
  level
  sprite
  tileset
  terrain_recipe
  prop_recipe
  PRIVATE
  absl::strings
)
