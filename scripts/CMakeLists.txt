add_executable(converter converter.cc)
target_include_directories(converter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(converter PRIVATE absl::log absl::status absl::strings absl::str_format)

add_executable(resize_image resize_image.cc)
target_include_directories(resize_image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(resize_image PRIVATE absl::log absl::status absl::strings absl::str_format)

add_executable(compose_blob47 compose_blob47.cc)
target_include_directories(compose_blob47 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(
  compose_blob47
  PRIVATE blob47_compose status_macros absl::log absl::log_initialize absl::status absl::strings
)

add_executable(render_slope_matrix render_slope_matrix.cc)
target_link_libraries(
  render_slope_matrix
  PRIVATE terrain_generator terrain_style image_io tileset status_macros absl::log
          absl::log_initialize absl::status absl::statusor absl::strings
)

add_executable(generate_blob47_mask generate_blob47_mask.cc)
target_include_directories(generate_blob47_mask PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(
  generate_blob47_mask
  PRIVATE terrain_mask absl::log absl::log_initialize absl::status absl::strings
)
