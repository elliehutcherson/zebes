# --- Editor Test Main ---
add_library(editor_test_main
  test_main.cc
)
target_link_libraries(editor_test_main
  PUBLIC
  imgui_tested
  imgui_test_engine
  SDL2-static
  absl::flags
  absl::flags_parse
  absl::log
)

# --- Sanity Test ---
add_executable(sanity_test sanity_test.cc)
target_link_libraries(sanity_test
  editor_test_main
  absl::flags
  absl::flags_parse
)

add_test(NAME sanity_test COMMAND sanity_test)


# --- Sprite Panel Test ---
add_executable(sprite_panel_test sprite_panel_test.cc)
target_link_libraries(sprite_panel_test
  editor_test_main
  sprite_panel
  editor_gui
  api
  gmock
  absl::flags
  absl::flags_parse
)
target_include_directories(sprite_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME sprite_panel_test COMMAND sprite_panel_test)


# --- Parallax Panel Unit Test ---
add_executable(parallax_panel_validation_test parallax_panel_validation_test.cc)
target_link_libraries(parallax_panel_validation_test
  gtest_main
  gmock
  api
  parallax_panel
  editor_gui
  imgui_tested
  imgui_test_engine
  SDL2-static
  absl::flags
  absl::flags_parse
)
target_include_directories(parallax_panel_validation_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME parallax_panel_validation_test COMMAND parallax_panel_validation_test)
