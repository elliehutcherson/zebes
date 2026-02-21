add_executable(converter converter.cc)
target_include_directories(converter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(converter PRIVATE absl::log absl::status absl::strings absl::str_format)

add_executable(resize_image resize_image.cc)
target_include_directories(resize_image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/stb)
target_link_libraries(resize_image PRIVATE absl::log absl::status absl::strings absl::str_format)
