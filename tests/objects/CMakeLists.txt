add_executable(terrain_cell_key_test terrain_cell_key_test.cc)
target_link_libraries(terrain_cell_key_test
  gtest_main
  gmock
  tileset
  absl::flat_hash_set
)
target_include_directories(terrain_cell_key_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_cell_key_test)

add_executable(tile_shape_geometry_test tile_shape_geometry_test.cc)
target_link_libraries(tile_shape_geometry_test
  gtest_main
  tile_shape_geometry
)
target_include_directories(tile_shape_geometry_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(tile_shape_geometry_test)

add_executable(level_test level_test.cc)
target_link_libraries(level_test level macros gtest_main)
target_include_directories(level_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(level_test)

add_executable(sprite_test sprite_test.cc)
target_link_libraries(sprite_test sprite gtest_main)
target_include_directories(sprite_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(sprite_test)
