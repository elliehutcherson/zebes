if(BUILD_UI_TESTING)
  # Shared harness for tests that require a real SDL window and ImGui context.
  add_library(editor_test_main test_main.cc)
  target_link_libraries(editor_test_main
    PUBLIC
    imgui_tested
    imgui_test_engine
    SDL2-static
    absl::flags
    absl::flags_parse
    absl::log
  )
endif()

# --- Gui Mock ---
add_library(gui_mock INTERFACE)
target_include_directories(gui_mock INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gui_mock INTERFACE
  gui_interface
  imgui_scoped
  gmock
)


if(BUILD_UI_TESTING)
  add_executable(sanity_test sanity_test.cc)
  target_link_libraries(sanity_test
    editor_test_main
    absl::flags
    absl::flags_parse
  )
  add_test(NAME sanity_test COMMAND sanity_test)

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

  set_tests_properties(sanity_test sprite_panel_test PROPERTIES LABELS "ui")
endif()


# --- Parallax Theme Panel Test ---
add_executable(sprite_panel_model_test sprite_panel_model_test.cc)
target_link_libraries(sprite_panel_model_test sprite_panel_model gtest_main)
gtest_discover_tests(sprite_panel_model_test)

add_executable(collider_panel_model_test collider_panel_model_test.cc)
target_link_libraries(collider_panel_model_test collider_panel_model gtest_main)
gtest_discover_tests(collider_panel_model_test)

add_executable(blueprint_panel_model_test blueprint_panel_model_test.cc)
target_link_libraries(blueprint_panel_model_test blueprint_panel_model gtest_main)
gtest_discover_tests(blueprint_panel_model_test)

add_executable(texture_editor_model_test texture_editor_model_test.cc)
target_link_libraries(texture_editor_model_test texture_editor_model gtest_main)
gtest_discover_tests(texture_editor_model_test)

add_executable(sprite_editor_model_test sprite_editor_model_test.cc)
target_link_libraries(sprite_editor_model_test sprite_editor_model gtest_main)
gtest_discover_tests(sprite_editor_model_test)

add_executable(animator_test animator_test.cc)
target_link_libraries(animator_test editor_animator gtest_main)
gtest_discover_tests(animator_test)

add_executable(tileset_editor_model_test tileset_editor_model_test.cc)
target_link_libraries(tileset_editor_model_test tileset_editor_model gtest_main)
gtest_discover_tests(tileset_editor_model_test)

# --- Parallax Theme Panel Test ---
add_executable(parallax_theme_panel_test parallax_theme_panel_test.cc)
target_link_libraries(parallax_theme_panel_test
  gtest_main
  gmock
  api
  parallax_theme_panel
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(parallax_theme_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME parallax_theme_panel_test COMMAND parallax_theme_panel_test)

# --- Parallax Zone Panel Test ---
add_executable(parallax_zone_panel_test parallax_zone_panel_test.cc)
target_link_libraries(parallax_zone_panel_test
  gtest_main
  gmock
  parallax_zone_panel
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(parallax_zone_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME parallax_zone_panel_test COMMAND parallax_zone_panel_test)

# --- Blueprint Palette Panel Test ---
add_executable(blueprint_palette_panel_test blueprint_palette_panel_test.cc)
target_link_libraries(blueprint_palette_panel_test
  gtest_main
  gmock
  api
  blueprint_palette_panel
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(blueprint_palette_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME blueprint_palette_panel_test COMMAND blueprint_palette_panel_test)

# --- Viewport Tab Test ---
add_executable(viewport_tab_test viewport_tab_test.cc)
target_link_libraries(viewport_tab_test
  gtest_main
  gmock
  api
  viewport_tab
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(viewport_tab_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME viewport_tab_test COMMAND viewport_tab_test)

# --- Parallax Layout Test ---
add_executable(parallax_layout_test parallax_layout_test.cc)
target_link_libraries(parallax_layout_test
  gtest_main
  parallax_layout
)
target_include_directories(parallax_layout_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME parallax_layout_test COMMAND parallax_layout_test)

# --- Mock Gui Test ---
add_executable(mock_gui_test mock_gui_test.cc)
target_link_libraries(mock_gui_test
  gtest_main
  gui_mock
)
add_test(NAME mock_gui_test COMMAND mock_gui_test)

# --- Level Panel Test ---
add_executable(level_panel_test level_panel_test.cc)
target_link_libraries(level_panel_test
  gtest_main
  gmock
  level_panel
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(level_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME level_panel_test COMMAND level_panel_test)

add_executable(level_panel_model_test level_panel_model_test.cc)
target_link_libraries(level_panel_model_test level_panel_model gtest_main)
gtest_discover_tests(level_panel_model_test)

# --- Level Editor Test ---
add_executable(level_editor_test level_editor_test.cc)
target_link_libraries(level_editor_test
  gtest_main
  gmock
  api
  level_editor
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(level_editor_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME level_editor_test COMMAND level_editor_test)

# --- Tile Palette Panel Test ---
add_executable(tile_palette_panel_test tile_palette_panel_test.cc)
target_link_libraries(tile_palette_panel_test
  gtest_main
  gmock
  api
  tile_palette_panel
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(tile_palette_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME tile_palette_panel_test COMMAND tile_palette_panel_test)

# --- Canvas Test ---
add_executable(canvas_test canvas_test.cc)
target_link_libraries(canvas_test
  gtest_main
  editor_canvas
  gui_mock
  absl::log
)
target_include_directories(canvas_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME canvas_test COMMAND canvas_test)
