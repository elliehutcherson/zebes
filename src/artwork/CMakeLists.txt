add_library(prop_artwork
  cleanup_prop.cc
  compose_prop.cc
  edge_treatment.cc
  isolate_subject.cc
  prop_artwork_pipeline.cc
  quantize_prop.cc
  rasterize_prop.cc
)

add_library(generated_artwork_postprocessor generated_artwork_postprocessor.cc)
target_link_libraries(generated_artwork_postprocessor
  PUBLIC
  image_io
  terrain_palette
  absl::status
  absl::statusor
  PRIVATE
  prop_artwork
  status_macros
  absl::strings
)

add_library(animation_frame_set_pipeline animation_frame_set_pipeline.cc)
target_link_libraries(animation_frame_set_pipeline
  PUBLIC
  image_io
  source_artwork
  sprite
  terrain_palette
  absl::status
  absl::statusor
  PRIVATE
  generated_artwork_postprocessor
  image_digest
  status_macros
  absl::strings
)

add_library(animation_frame_set_recipe animation_frame_set_recipe.cc)
target_link_libraries(animation_frame_set_recipe
  PUBLIC
  animation_frame_set_pipeline
  sprite
  nlohmann_json::nlohmann_json
  absl::status
  absl::statusor
  PRIVATE
  blueprint
  image_digest
  status_macros
  absl::strings
)

add_library(prepare_animation_frame_set_asset prepare_animation_frame_set_asset.cc)
target_link_libraries(prepare_animation_frame_set_asset
  PUBLIC
  animation_frame_set_pipeline
  animation_frame_set_recipe
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

add_library(regenerate_animation_frame_set_asset regenerate_animation_frame_set_asset.cc)
target_link_libraries(regenerate_animation_frame_set_asset
  PUBLIC
  animation_frame_set_pipeline
  animation_frame_set_recipe
  source_artwork
  blueprint
  sprite
  texture
  image_io
  absl::status
  absl::statusor
  PRIVATE
  prepare_animation_frame_set_asset
  image_digest
  status_macros
  absl::strings
)

add_library(delete_animation_frame_set_asset delete_animation_frame_set_asset.cc)
target_link_libraries(delete_animation_frame_set_asset
  PUBLIC
  animation_frame_set_recipe
  source_artwork
  blueprint
  sprite
  texture
  image_io
  absl::status
  absl::statusor
  PRIVATE
  image_digest
  status_macros
  absl::strings
)

add_library(animation_artwork_feasibility animation_artwork_feasibility.cc)
target_link_libraries(animation_artwork_feasibility
  PUBLIC
  image_io
  sprite
  terrain_palette
  absl::status
  absl::statusor
  PRIVATE
  generated_artwork_postprocessor
  image_digest
  status_macros
  absl::strings
)

add_library(profile_silhouette profile_silhouette.cc)
target_link_libraries(profile_silhouette
  PUBLIC
  image_io
  absl::status
  absl::statusor
  PRIVATE
  status_macros
  absl::strings
)

add_library(repetition_review repetition_review.cc)
target_link_libraries(repetition_review
  PUBLIC
  image_io
  absl::statusor
  PRIVATE
  absl::status
)

add_library(parallax_artwork_pipeline parallax_artwork_pipeline.cc)
target_link_libraries(parallax_artwork_pipeline
  PUBLIC
  image_io
  repetition_review
  source_artwork
  terrain_palette
  absl::status
  absl::statusor
  PRIVATE
  generated_artwork_postprocessor
  image_digest
  status_macros
  absl::strings
)

add_library(parallax_artwork_recipe parallax_artwork_recipe.cc)
target_link_libraries(parallax_artwork_recipe
  PUBLIC
  parallax_artwork_pipeline
  nlohmann_json::nlohmann_json
  absl::status
  absl::statusor
  PRIVATE
  image_digest
  status_macros
  absl::strings
)

add_library(prepare_parallax_artwork_asset prepare_parallax_artwork_asset.cc)
target_link_libraries(prepare_parallax_artwork_asset
  PUBLIC
  parallax_artwork_pipeline
  parallax_artwork_recipe
  source_artwork
  texture
  absl::status
  absl::statusor
  PRIVATE
  common
  image_digest
  status_macros
  absl::strings
)

add_library(regenerate_parallax_artwork_asset regenerate_parallax_artwork_asset.cc)
target_link_libraries(regenerate_parallax_artwork_asset
  PUBLIC
  parallax_artwork_pipeline
  parallax_artwork_recipe
  source_artwork
  texture
  image_io
  absl::status
  absl::statusor
  PRIVATE
  prepare_parallax_artwork_asset
  image_digest
  status_macros
)

add_library(redraw_parallax_artwork_asset redraw_parallax_artwork_asset.cc)
target_link_libraries(redraw_parallax_artwork_asset
  PUBLIC
  parallax_artwork_pipeline
  parallax_artwork_recipe
  source_artwork
  texture
  image_io
  absl::status
  absl::statusor
  PRIVATE
  prepare_parallax_artwork_asset
  image_digest
  status_macros
  nlohmann_json::nlohmann_json
)

target_link_libraries(prop_artwork
  PUBLIC
  image_io
  image_digest
  prop_artwork_style
  source_artwork
  terrain_palette
  absl::span
  absl::status
  absl::statusor
  PRIVATE
  absl::strings
  status_macros
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
  image_io
  absl::status
  absl::statusor
  nlohmann_json::nlohmann_json
  PRIVATE
  image_digest
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
  image_digest
  prop_artwork
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
