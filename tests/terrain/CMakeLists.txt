add_executable(terrain_mask_test terrain_mask_test.cc)
target_link_libraries(terrain_mask_test
  gtest_main
  terrain_mask
)
target_include_directories(terrain_mask_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_mask_test)

add_executable(blob47_compose_test blob47_compose_test.cc)
target_link_libraries(blob47_compose_test
  gtest_main
  blob47_compose
)
target_include_directories(blob47_compose_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(blob47_compose_test)

add_executable(terrain_detect_test terrain_detect_test.cc)
target_link_libraries(terrain_detect_test
  gtest_main
  terrain_detect
  blob47_compose
)
target_include_directories(terrain_detect_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_detect_test)
