add_executable(converter converter.cc)
target_include_directories(converter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(converter PRIVATE absl::log absl::status absl::strings absl::str_format)

add_executable(resize_image resize_image.cc)
target_include_directories(resize_image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(resize_image PRIVATE absl::log absl::status absl::strings absl::str_format)

add_executable(compose_blob47 compose_blob47.cc)
target_link_libraries(
  compose_blob47
  PRIVATE blob47_compose image_io status_macros absl::log absl::log_initialize absl::status
          absl::strings
)

add_executable(render_slope_matrix render_slope_matrix.cc)
target_link_libraries(
  render_slope_matrix
  PRIVATE terrain_review_scene terrain_style image_io status_macros absl::log
          absl::log_initialize absl::status absl::statusor absl::strings
)

add_executable(generate_blob47_mask generate_blob47_mask.cc)
target_include_directories(generate_blob47_mask PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(
  generate_blob47_mask
  PRIVATE terrain_mask status_macros absl::log absl::log_initialize absl::status absl::strings
)

add_executable(prop_artwork_spike prop_artwork_spike.cc)
target_link_libraries(
  prop_artwork_spike
  PRIVATE prop_artwork terrain_generator terrain_palette terrain_recipe image_io status_macros
          nlohmann_json::nlohmann_json absl::log absl::log_initialize absl::status
          absl::statusor absl::strings
)

add_executable(process_generated_artwork process_generated_artwork.cc)
target_link_libraries(process_generated_artwork
  PRIVATE
  generated_artwork_postprocessor
  image_io
  absl::flags
  absl::flags_parse
  absl::status
  absl::statusor
)

add_executable(curate_assets curate_assets.cc)
target_link_libraries(curate_assets
  PRIVATE
  asset_workspace
  curation_candidate_commit
  curation_registry
  curation_review
  headless_texture_store
  level_reviewer
  parallax_artwork_reviewer
  parallax_theme_reviewer
  prop_reviewer
  sprite_reviewer
  terrain_reviewer
  tileset_reviewer
  status_macros
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
  absl::status
  absl::statusor
  absl::strings
  nlohmann_json::nlohmann_json
)
add_dependencies(curate_assets copy_assets)

add_executable(generate_assets generate_assets.cc)
target_link_libraries(generate_assets
  PRIVATE
  asset_workspace
  fake_image_generation_client
  headless_asset_generation
  headless_texture_store
  image_generation_service
  status_macros
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
  absl::status
  absl::statusor
  absl::strings
)
add_dependencies(generate_assets copy_assets)

add_executable(stage_asset_creation stage_asset_creation.cc)
target_link_libraries(stage_asset_creation
  PRIVATE
  asset_workspace
  headless_asset_generation
  headless_texture_store
  image_io
  status_macros
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
  absl::status
  absl::strings
)
add_dependencies(stage_asset_creation copy_assets)

add_executable(stage_asset_redraw stage_asset_redraw.cc)
target_link_libraries(stage_asset_redraw
  PRIVATE
  asset_workspace
  atomic_directory_publisher
  generated_asset_candidate
  image_digest
  image_io
  headless_texture_store
  status_macros
  utc_timestamp
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
  absl::status
  absl::statusor
  absl::strings
  nlohmann_json::nlohmann_json
)

add_executable(build_environment build_environment.cc)
target_link_libraries(build_environment
  PRIVATE
  asset_workspace
  environment_builder
  headless_texture_store
  status_macros
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
  absl::status
  absl::statusor
  absl::strings
)
add_dependencies(build_environment copy_assets)
