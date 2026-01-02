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


# --- Blueprint Editor Test ---
add_executable(blueprint_editor_test blueprint_editor_test.cc)
target_link_libraries(blueprint_editor_test
  editor_test_main
  blueprint_editor
  blueprint_manager
  api
  macros
  gmock
  absl::flags
  absl::flags_parse
)
target_include_directories(blueprint_editor_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME blueprint_editor_test COMMAND blueprint_editor_test)

# --- Sprite Panel Test ---
add_executable(sprite_panel_test
  sprite_panel_test.cc
  ${CMAKE_SOURCE_DIR}/src/editor/blueprint/sprite_panel.cc
  ${CMAKE_SOURCE_DIR}/src/editor/editor_utils.cc
)
target_link_libraries(sprite_panel_test
  editor_test_main
  imgui_test_engine
  api
  macros
  gmock
  absl::flags
  absl::flags_parse
  sprite
  absl::status
)
target_include_directories(sprite_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME sprite_panel_test COMMAND sprite_panel_test)
