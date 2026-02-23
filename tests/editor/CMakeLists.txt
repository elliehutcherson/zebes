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

# --- Gui Mock ---
add_library(gui_mock INTERFACE)
target_include_directories(gui_mock INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gui_mock INTERFACE
  gui_interface
  imgui_scoped
  gmock
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
  api
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
  viewport_tab
)
target_include_directories(viewport_tab_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME viewport_tab_test COMMAND viewport_tab_test)

# --- Mock Gui Test ---
add_executable(mock_gui_test mock_gui_test.cc)
target_link_libraries(mock_gui_test
  gtest_main
  gui_mock
)
add_test(NAME mock_gui_test COMMAND mock_gui_test)

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
