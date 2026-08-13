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

add_executable(terrain_field_test terrain_field_test.cc)
target_link_libraries(terrain_field_test
  gtest_main
  terrain_field
)
target_include_directories(terrain_field_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_field_test)

add_executable(terrain_motifs_test terrain_motifs_test.cc)
target_link_libraries(terrain_motifs_test
  gtest_main
  terrain_motifs
)
target_include_directories(terrain_motifs_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_motifs_test)

add_executable(terrain_generator_test terrain_generator_test.cc)
target_link_libraries(terrain_generator_test
  gtest_main
  terrain_generator
  terrain_detect
  absl::flat_hash_set
)
target_include_directories(terrain_generator_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_generator_test)

add_executable(terrain_detect_test terrain_detect_test.cc)
target_link_libraries(terrain_detect_test
  gtest_main
  gmock
  terrain_detect
  blob47_compose
)
target_include_directories(terrain_detect_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_detect_test)
