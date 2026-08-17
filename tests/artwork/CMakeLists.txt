add_executable(prop_artwork_pipeline_test prop_artwork_pipeline_test.cc)
target_link_libraries(prop_artwork_pipeline_test
  gtest_main
  macros
  prop_artwork
  terrain_palette
)
target_include_directories(prop_artwork_pipeline_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prop_artwork_pipeline_test)

add_executable(prepare_prop_asset_test prepare_prop_asset_test.cc)
target_link_libraries(prepare_prop_asset_test
  gtest_main
  macros
  prepare_prop_asset
  terrain_palette
  image_digest
  status_macros
)
target_include_directories(prepare_prop_asset_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prepare_prop_asset_test)
