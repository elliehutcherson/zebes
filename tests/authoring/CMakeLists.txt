add_executable(environment_builder_test environment_builder_test.cc)
target_link_libraries(environment_builder_test
  PRIVATE
  environment_builder
  macros
  absl::status
  gtest_main
  nlohmann_json::nlohmann_json
)
target_compile_definitions(environment_builder_test
  PRIVATE ZEBES_TEST_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
gtest_discover_tests(environment_builder_test)

add_executable(terrain_builder_test terrain_builder_test.cc)
target_link_libraries(terrain_builder_test
  PRIVATE
  terrain_builder
  macros
  absl::status
  gtest_main
  gmock
  nlohmann_json::nlohmann_json
)
target_compile_definitions(terrain_builder_test
  PRIVATE ZEBES_TEST_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
gtest_discover_tests(terrain_builder_test)
