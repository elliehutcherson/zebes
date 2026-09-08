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
  PRIVATE
  json_schema
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

add_library(skeleton_rig skeleton_rig.cc)
target_link_libraries(skeleton_rig
  PRIVATE
  json_schema
  PUBLIC
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  status_macros
  absl::flat_hash_set
  absl::status
  absl::strings
  nlohmann_json::nlohmann_json
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

add_library(profile_deformation profile_deformation.cc)
target_link_libraries(profile_deformation
  PUBLIC
  image_io
  profile_silhouette
  absl::span
  absl::status
  absl::statusor
  PRIVATE
  status_macros
)

add_library(layered_puppet layered_puppet.cc)
target_link_libraries(layered_puppet
  PUBLIC
  image_io
  profile_silhouette
  absl::span
  absl::status
  absl::statusor
  PRIVATE
  status_macros
)

add_library(layered_puppet_diagnostics layered_puppet_diagnostics.cc)
target_link_libraries(layered_puppet_diagnostics
  PUBLIC
  image_io
  layered_puppet
  profile_silhouette
  absl::span
  absl::statusor
  PRIVATE
  semantic_layer_import
  status_macros
  absl::status
)

add_library(puppet_document puppet_document.cc)
target_link_libraries(puppet_document
  PUBLIC
  layered_puppet
  profile_silhouette
  skeleton_rig
  absl::status
  PRIVATE
  status_macros
  absl::flat_hash_set
  absl::strings
  absl::str_format
)

add_library(puppet_document_json puppet_document_json.cc)
target_link_libraries(puppet_document_json
  PUBLIC
  puppet_document
  absl::status
  absl::statusor
  PRIVATE
  json_schema
  layered_puppet
  profile_silhouette
  skeleton_rig
  status_macros
  utc_timestamp
  absl::strings
  nlohmann_json::nlohmann_json
)

# The editor page is authored as real HTML so it gets an editor, a formatter and
# a linter, then embedded so the tool stays one binary. --page overrides it for
# live editing without a rebuild.
set(PUPPET_EDITOR_HTML_HEADER ${CMAKE_CURRENT_BINARY_DIR}/puppet_editor_html.h)
add_custom_command(
  OUTPUT ${PUPPET_EDITOR_HTML_HEADER}
  COMMAND ${CMAKE_COMMAND}
          -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/puppet_editor.html
          -DOUTPUT=${PUPPET_EDITOR_HTML_HEADER}
          -DSYMBOL=kPuppetEditorHtml
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_text_file.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/puppet_editor.html
          ${CMAKE_SOURCE_DIR}/cmake/embed_text_file.cmake
  COMMENT "Embedding puppet_editor.html"
  VERBATIM
)

add_library(puppet_editor_page puppet_editor_page.cc ${PUPPET_EDITOR_HTML_HEADER})
target_include_directories(puppet_editor_page PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_library(puppet_build_json puppet_build_json.cc)
target_link_libraries(puppet_build_json
  PUBLIC
  layered_puppet
  PRIVATE
  profile_silhouette
  nlohmann_json::nlohmann_json
)

add_library(puppet_editor_session puppet_editor_session.cc)
target_link_libraries(puppet_editor_session
  PUBLIC
  image_io
  layered_puppet
  layered_puppet_spec
  puppet_document
  absl::span
  absl::status
  absl::statusor
  PRIVATE
  puppet_document_json
  status_macros
  absl::strings
)

add_library(puppet_editor_workspace puppet_editor_workspace.cc)
target_link_libraries(puppet_editor_workspace
  PUBLIC
  puppet_editor_session
  absl::status
  absl::statusor
  PRIVATE
  puppet_document
  puppet_document_json
  status_macros
  absl::strings
)

add_library(layered_puppet_spec layered_puppet_spec.cc)
target_link_libraries(layered_puppet_spec
  PUBLIC
  image_io
  layered_puppet
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  semantic_layer_import
  status_macros
  absl::span
  absl::status
  absl::str_format
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(semantic_layer_import semantic_layer_import.cc)
target_link_libraries(semantic_layer_import
  PUBLIC
  image_io
  absl::span
  absl::statusor
  PRIVATE
  absl::status
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
  PRIVATE
  json_schema
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
  PRIVATE
  json_schema
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
  PRIVATE
  json_schema
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
