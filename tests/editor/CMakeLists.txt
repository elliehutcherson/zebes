# --- Editor Test Main ---
add_library(editor_test_main
  test_main.cc
)
target_link_libraries(editor_test_main
  PUBLIC
  imgui_tested
  imgui_test_engine
  SDL2-static
)

# --- Sanity Test ---
add_executable(sanity_test sanity_test.cc)
target_link_libraries(sanity_test
  editor_test_main
)

add_test(NAME sanity_test COMMAND sanity_test)


# --- Sprite Panel Test ---
add_executable(sprite_panel_test sprite_panel_test.cc)
target_link_libraries(sprite_panel_test
  editor_test_main
  sprite_panel
  api
  gmock
  absl::flags
  absl::flags_parse
)
target_include_directories(sprite_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME sprite_panel_test COMMAND sprite_panel_test)


# --- Level Editor Test ---
add_executable(level_editor_test level_editor_test.cc)
target_link_libraries(level_editor_test
  editor_test_main
  level_editor
  api
  gmock
  imgui_tested
  imgui_test_engine
  SDL2-static
  absl::flags
  absl::flags_parse
)
target_include_directories(level_editor_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME level_editor_test COMMAND level_editor_test)


# --- Level Panel Test ---
add_executable(level_panel_test level_panel_test.cc)
target_link_libraries(level_panel_test
  editor_test_main
  level_panel
  api
  gmock
  imgui_tested
  imgui_test_engine
  SDL2-static
  absl::flags
  absl::flags_parse
)
target_include_directories(level_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME level_panel_test COMMAND level_panel_test)


# --- Parallax Panel Test ---
add_executable(parallax_panel_test parallax_panel_test.cc)
target_link_libraries(parallax_panel_test
  editor_test_main
  parallax_panel
  imgui_tested
  imgui_test_engine
  SDL2-static
  absl::flags
  absl::flags_parse
)
target_include_directories(parallax_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME parallax_panel_test COMMAND parallax_panel_test)

