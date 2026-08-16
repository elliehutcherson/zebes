add_executable(prop_artwork_pipeline_test prop_artwork_pipeline_test.cc)
target_link_libraries(prop_artwork_pipeline_test
  gtest_main
  macros
  prop_artwork
  terrain_palette
)
target_include_directories(prop_artwork_pipeline_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prop_artwork_pipeline_test)
