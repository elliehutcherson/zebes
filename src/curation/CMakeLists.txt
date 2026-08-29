add_library(curation_raster raster_canvas.cc)
target_link_libraries(curation_raster
  PUBLIC image_io absl::status absl::statusor
  PRIVATE status_macros absl::strings
)

add_library(curation_review review.cc)
target_link_libraries(curation_review
  PUBLIC image_io nlohmann_json::nlohmann_json absl::any_invocable absl::status absl::statusor
  PRIVATE atomic_directory_publisher image_digest resource_utils status_macros
)

add_library(curation_registry registry.cc)
target_link_libraries(curation_registry
  PUBLIC curation_review
  PRIVATE absl::status absl::statusor absl::strings
)

add_library(curation_candidate_commit candidate_commit.cc)
target_link_libraries(curation_candidate_commit
  PUBLIC curation_registry
  PRIVATE atomic_directory_publisher status_macros absl::status
)

add_library(level_review_route level_review_route.cc)
target_link_libraries(level_review_route
  PUBLIC level parallax_preview_model absl::statusor absl::span
  PRIVATE status_macros absl::status absl::strings absl::str_format
)

add_library(prop_candidate prop_candidate.cc)
target_link_libraries(prop_candidate
  PUBLIC
  api
  blueprint
  image_io
  prop_recipe
  sprite
  nlohmann_json::nlohmann_json
  absl::status
  absl::statusor
  PRIVATE
  generated_asset_candidate
  image_digest
  prepare_prop_asset
  regenerate_prop_asset
  retained_source_transaction
  status_macros
  absl::strings
)

add_library(level_reviewer level_reviewer.cc)
target_link_libraries(level_reviewer
  PUBLIC curation_registry
  PRIVATE
  api
  curation_raster
  level_manager
  level_review_route
  parallax_diagnostics
  parallax_layout
  parallax_preview_model
  prop_candidate
  scene_composition
  status_macros
  tileset_manager
  absl::status
  absl::statusor
  absl::strings
  absl::str_format
)

add_library(parallax_theme_reviewer parallax_theme_reviewer.cc)
target_link_libraries(parallax_theme_reviewer
  PUBLIC curation_registry
  PRIVATE
  api
  curation_raster
  parallax_diagnostics
  parallax_layout
  parallax_preview_model
  status_macros
  absl::status
  absl::statusor
  absl::strings
)

add_library(parallax_artwork_reviewer parallax_artwork_reviewer.cc)
target_link_libraries(parallax_artwork_reviewer
  PUBLIC curation_registry
  PRIVATE
  api
  curation_raster
  image_digest
  generated_asset_candidate
  parallax_artwork_recipe
  prepare_parallax_artwork_asset
  regenerate_parallax_artwork_asset
  retained_source_transaction
  repetition_review
  status_macros
  absl::status
  absl::statusor
  absl::strings
)

add_library(prop_reviewer prop_reviewer.cc)
target_link_libraries(prop_reviewer
  PUBLIC curation_registry
  PRIVATE
  api
  curation_raster
  image_digest
  prop_candidate
  prop_recipe
  status_macros
  absl::status
  absl::statusor
  absl::strings
)

add_library(tileset_reviewer tileset_reviewer.cc)
target_link_libraries(tileset_reviewer
  PUBLIC curation_registry
  PRIVATE api curation_raster tileset_manager status_macros absl::status absl::statusor
          absl::strings
)

add_library(sprite_reviewer sprite_reviewer.cc)
target_link_libraries(sprite_reviewer
  PUBLIC curation_registry
  PRIVATE api curation_raster status_macros absl::status absl::statusor absl::strings
)

add_library(terrain_reviewer terrain_reviewer.cc)
target_link_libraries(terrain_reviewer
  PUBLIC curation_registry
  PRIVATE api curation_raster terrain_recipe terrain_review_scene tileset_manager status_macros
          absl::status absl::statusor absl::strings
)
