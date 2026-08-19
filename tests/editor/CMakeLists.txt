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

add_executable(texture_editor_test texture_editor_test.cc)
target_link_libraries(texture_editor_test
  gtest_main
  gmock
  macros
  api
  editor_texture
  sdl_wrapper
  gui_mock
)
target_include_directories(texture_editor_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(texture_editor_test)

add_executable(texture_preview_test texture_preview_test.cc)
target_link_libraries(texture_preview_test editor_texture_preview gtest_main)
gtest_discover_tests(texture_preview_test)

add_executable(sprite_editor_model_test sprite_editor_model_test.cc)
target_link_libraries(sprite_editor_model_test sprite_editor_model gtest_main)
gtest_discover_tests(sprite_editor_model_test)

add_executable(animator_test animator_test.cc)
target_link_libraries(animator_test editor_animator gtest_main)
gtest_discover_tests(animator_test)

add_executable(tileset_editor_model_test tileset_editor_model_test.cc)
target_link_libraries(tileset_editor_model_test
  tileset_editor_model
  blob47_compose
  terrain_mask
  gtest_main
  gmock
)
target_include_directories(tileset_editor_model_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(tileset_editor_model_test)

# --- Terrain Editor ---
add_executable(terrain_creation_test terrain_creation_test.cc)
target_link_libraries(terrain_creation_test
  terrain_creation
  gtest_main
  gmock
)
target_include_directories(terrain_creation_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_creation_test)

add_executable(terrain_editor_model_test terrain_editor_model_test.cc)
target_link_libraries(terrain_editor_model_test
  terrain_editor_model
  gtest_main
  gmock
)
target_include_directories(terrain_editor_model_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_editor_model_test)

add_executable(terrain_controls_panel_test terrain_controls_panel_test.cc)
target_link_libraries(terrain_controls_panel_test
  terrain_controls_panel
  gtest_main
  gmock
)
target_include_directories(terrain_controls_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_controls_panel_test)

add_executable(terrain_output_panel_test terrain_output_panel_test.cc)
target_link_libraries(terrain_output_panel_test
  terrain_output_panel
  gtest_main
  gmock
  macros
)
target_include_directories(terrain_output_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_output_panel_test)

add_executable(terrain_editor_test terrain_editor_test.cc)
target_link_libraries(terrain_editor_test
  gtest_main
  gmock
  macros
  api
  terrain_editor
  gui_mock
)
target_include_directories(terrain_editor_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_editor_test)

add_executable(prop_artwork_editor_model_test prop_artwork_editor_model_test.cc)
target_link_libraries(prop_artwork_editor_model_test
  gtest_main
  macros
  prop_artwork_editor_model
  prepare_prop_asset
  terrain_palette
)
target_include_directories(prop_artwork_editor_model_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prop_artwork_editor_model_test)

add_executable(prop_artwork_context_test prop_artwork_context_test.cc)
target_link_libraries(prop_artwork_context_test
  gtest_main
  macros
  prop_artwork_context
)
target_include_directories(prop_artwork_context_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prop_artwork_context_test)

add_executable(prop_artwork_controls_panel_test prop_artwork_controls_panel_test.cc)
target_link_libraries(prop_artwork_controls_panel_test
  gtest_main
  gmock
  macros
  image_digest
  prop_artwork_controls_panel
  gui_mock
)
target_include_directories(prop_artwork_controls_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prop_artwork_controls_panel_test)

add_executable(prop_artwork_output_panel_test prop_artwork_output_panel_test.cc)
target_link_libraries(prop_artwork_output_panel_test
  gtest_main
  gmock
  macros
  prop_artwork_output_panel
  gui_mock
)
target_include_directories(prop_artwork_output_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prop_artwork_output_panel_test)

add_executable(prop_artwork_editor_test prop_artwork_editor_test.cc)
target_link_libraries(prop_artwork_editor_test
  gtest_main
  gmock
  macros
  image_generation
  image_generation_engine
  prop_artwork_editor
  gui_mock
)
target_include_directories(prop_artwork_editor_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(prop_artwork_editor_test)

add_executable(openai_image_client_test openai_image_client_test.cc)
target_link_libraries(openai_image_client_test
  PRIVATE
  image_io
  macros
  openai_image_client
  status_macros
  nlohmann_json::nlohmann_json
  gtest_main
)
target_include_directories(openai_image_client_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(openai_image_client_test)

add_executable(image_generation_engine_test image_generation_engine_test.cc)
target_link_libraries(image_generation_engine_test
  PRIVATE
  blocking_callback_thread
  engine_runner
  image_generation
  image_generation_engine
  macros
  status_macros
  gtest_main
)
target_include_directories(image_generation_engine_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(image_generation_engine_test)

add_executable(image_generation_service_test image_generation_service_test.cc)
target_link_libraries(image_generation_service_test
  PRIVATE
  image_generation
  image_generation_engine
  image_generation_service
  macros
  status_macros
  absl::status
  absl::statusor
  absl::strings
  absl::time
  gtest_main
)
target_include_directories(image_generation_service_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(image_generation_service_test)

add_executable(image_generation_test image_generation_test.cc)
target_link_libraries(image_generation_test
  gtest_main
  macros
  curl_http_transport
  http_transport
  image_generation
)
target_include_directories(image_generation_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(image_generation_test)

# --- Tileset Panel Test ---
add_executable(tileset_panel_test tileset_panel_test.cc)
target_link_libraries(tileset_panel_test
  gtest_main
  gmock
  macros
  tileset_panel
  tileset_editor_model
  blob47_compose
  gui_mock
)
target_include_directories(tileset_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(tileset_panel_test)

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
  macros
  terrain_brush
  viewport_tab
  viewport_model
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(viewport_tab_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME viewport_tab_test COMMAND viewport_tab_test)

add_executable(level_tiles_test level_tiles_test.cc)
target_link_libraries(level_tiles_test
  gtest_main
  level_tiles
)
target_include_directories(level_tiles_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(level_tiles_test)

add_executable(viewport_model_test viewport_model_test.cc)
target_link_libraries(viewport_model_test
  gtest_main
  viewport_model
)
target_include_directories(viewport_model_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(viewport_model_test)

add_executable(viewport_scene_test viewport_scene_test.cc)
target_link_libraries(viewport_scene_test
  gtest_main
  macros
  viewport_scene
)
target_include_directories(viewport_scene_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(viewport_scene_test)

add_executable(derived_artwork_test derived_artwork_test.cc)
target_link_libraries(derived_artwork_test
  gtest_main
  macros
  status_macros
  derived_tile_provider
  terrain_brush
  terrain_content_index
  viewport_model
)
target_include_directories(derived_artwork_test PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests)
gtest_discover_tests(derived_artwork_test)

add_executable(derived_terrain_session_test derived_terrain_session_test.cc)
target_link_libraries(derived_terrain_session_test
  gtest_main
  gmock
  macros
  derived_terrain_session
)
target_include_directories(derived_terrain_session_test PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests)
gtest_discover_tests(derived_terrain_session_test)

add_executable(derived_tile_provider_test derived_tile_provider_test.cc)
target_link_libraries(derived_tile_provider_test
  gtest_main
  macros
  derived_tile_provider
  terrain_mask
)
target_include_directories(derived_tile_provider_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(derived_tile_provider_test)

add_executable(terrain_brush_test terrain_brush_test.cc)
target_link_libraries(terrain_brush_test
  gtest_main
  macros
  terrain_brush
  terrain_mask
  viewport_model
)
target_include_directories(terrain_brush_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(terrain_brush_test)

add_executable(viewport_interaction_test viewport_interaction_test.cc)
target_link_libraries(viewport_interaction_test
  gtest_main
  terrain_mask
  viewport_interaction
  viewport_model
)
target_include_directories(viewport_interaction_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(viewport_interaction_test)

# --- Parallax Layout Test ---
add_executable(parallax_layout_test parallax_layout_test.cc)
target_link_libraries(parallax_layout_test
  gtest_main
  parallax_layout
)
target_include_directories(parallax_layout_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME parallax_layout_test COMMAND parallax_layout_test)

add_executable(camera_guide_test camera_guide_test.cc)
target_link_libraries(camera_guide_test
  gtest_main
  camera_guide
)
target_include_directories(camera_guide_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME camera_guide_test COMMAND camera_guide_test)

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

add_executable(world_layer_model_test world_layer_model_test.cc)
target_link_libraries(world_layer_model_test world_layer_model macros gtest_main)
gtest_discover_tests(world_layer_model_test)

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

# --- Terrain Palette Panel Test ---
add_executable(terrain_palette_panel_test terrain_palette_panel_test.cc)
target_link_libraries(terrain_palette_panel_test
  gtest_main
  gmock
  api
  terrain_palette_panel
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(terrain_palette_panel_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME terrain_palette_panel_test COMMAND terrain_palette_panel_test)

# --- Canvas Test ---
# --- Tileset Editor Test ---
add_executable(tileset_editor_test tileset_editor_test.cc)
target_link_libraries(tileset_editor_test
  gtest_main
  gmock
  api
  tileset_editor
  gui_mock
  absl::flags
  absl::flags_parse
)
target_include_directories(tileset_editor_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME tileset_editor_test COMMAND tileset_editor_test)

# --- Confirm Prompt Test ---
add_executable(confirm_prompt_test confirm_prompt_test.cc)
target_link_libraries(confirm_prompt_test
  gtest_main
  gmock
  confirm_prompt
  gui_mock
)
target_include_directories(confirm_prompt_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME confirm_prompt_test COMMAND confirm_prompt_test)

add_executable(canvas_test canvas_test.cc)
target_link_libraries(canvas_test
  gtest_main
  editor_canvas
  gui_mock
  absl::log
)
target_include_directories(canvas_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME canvas_test COMMAND canvas_test)
