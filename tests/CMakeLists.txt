cmake_policy(SET CMP0079 NEW)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

# Bring in the GoogleTest CMake module (required for discovery)
include(GoogleTest)
find_package(Threads REQUIRED)

# CMake 4.4.0's POST_BUILD discovery omits the test target when choosing its
# temporary JSON filename. Parallel links in one directory can therefore
# remove or parse the same file concurrently. PRE_TEST discovery is serialized
# by CTest and avoids making a successful parallel build depend on discovery.
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

# Override output directory specifically for tests (as you had before)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

# Library for status macros
add_library(macros INTERFACE macros.h)
target_link_libraries(macros INTERFACE absl::statusor)

add_executable(common_config_test common_config_test.cc)
target_link_libraries(common_config_test config gtest_main)
gtest_discover_tests(common_config_test)

add_executable(image_io_test common/image_io_test.cc)
target_link_libraries(image_io_test image_io gtest_main)

gtest_discover_tests(image_io_test)

add_executable(image_digest_test common/image_digest_test.cc)
target_link_libraries(image_digest_test image_digest macros gtest_main)
gtest_discover_tests(image_digest_test)

add_executable(background_task_test common/background_task_test.cc)
target_link_libraries(background_task_test background_task macros gtest_main)
gtest_discover_tests(background_task_test)

add_executable(mpsc_queue_test common/mpsc_queue_test.cc)
target_link_libraries(mpsc_queue_test mpsc_queue gtest_main Threads::Threads)
gtest_discover_tests(mpsc_queue_test)

add_executable(engine_runner_test common/engine_runner_test.cc)
target_link_libraries(engine_runner_test
  blocking_callback_thread
  engine_runner
  macros
  mpsc_queue
  status_macros
  gtest_main
)
gtest_discover_tests(engine_runner_test)

add_executable(notification_set_test common/notification_set_test.cc)
target_link_libraries(notification_set_test
  blocking_callback_thread
  notification_set
  macros
  gtest_main
)
gtest_discover_tests(notification_set_test)

add_executable(resource_utils_test resources/resource_utils_test.cc)
target_link_libraries(resource_utils_test resource_utils gtest_main gmock)
gtest_discover_tests(resource_utils_test)

# --- Texture Manager ---
add_executable(texture_manager_test resources/texture_manager_test.cc)
target_link_libraries(texture_manager_test texture_manager macros gtest_main)
gtest_discover_tests(texture_manager_test) # <--- REGISTERS THE TEST

# --- Sprite Manager ---
add_executable(sprite_manager_test resources/sprite_manager_test.cc)
target_link_libraries(sprite_manager_test sprite_manager texture_manager macros gtest_main gmock)
gtest_discover_tests(sprite_manager_test) # <--- REGISTERS THE TEST

# --- Collider Manager ---
add_executable(collider_manager_test resources/collider_manager_test.cc)
target_link_libraries(collider_manager_test collider_manager macros gtest_main)
gtest_discover_tests(collider_manager_test) # <--- REGISTERS THE TEST

# --- Blueprint Manager ---
add_executable(blueprint_manager_test resources/blueprint_manager_test.cc)
target_link_libraries(blueprint_manager_test blueprint_manager macros gtest_main)
gtest_discover_tests(blueprint_manager_test) # <--- REGISTERS THE TEST

# --- API Validation ---
add_executable(api_validation_test api_validation_test.cc)
target_link_libraries(api_validation_test
  api
  macros
  gtest_main
  gmock
  absl::flags
  absl::flags_parse
)
gtest_discover_tests(api_validation_test) # <--- REGISTERS THE TEST

add_executable(asset_root_lock_test api/asset_root_lock_test.cc)
target_link_libraries(asset_root_lock_test asset_root_lock macros gtest_main gmock)
gtest_discover_tests(asset_root_lock_test)

add_executable(asset_workspace_test api/asset_workspace_test.cc)
target_link_libraries(asset_workspace_test
  asset_workspace headless_texture_store macros gtest_main)
target_compile_definitions(asset_workspace_test
  PRIVATE ZEBES_TEST_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
gtest_discover_tests(asset_workspace_test)

add_executable(input_manager_test input_manager_test.cc)
target_link_libraries(input_manager_test input_manager macros gtest_main gmock)
gtest_discover_tests(input_manager_test)

add_executable(sdl_input_source_test platform/sdl_input_source_test.cc)
target_link_libraries(sdl_input_source_test
  sdl_input_source
  sdl_wrapper
  imgui_wrapper
  gtest_main
  gmock
)
gtest_discover_tests(sdl_input_source_test)

add_executable(sdl_texture_store_test platform/sdl_texture_store_test.cc)
target_link_libraries(sdl_texture_store_test sdl_texture_store sdl_wrapper gtest_main)
gtest_discover_tests(sdl_texture_store_test)

# --- Camera Controller ---
add_executable(camera_controller_test engine/camera_controller_test.cc)
target_link_libraries(camera_controller_test camera_controller macros gtest_main gmock)
gtest_discover_tests(camera_controller_test)

add_executable(scene_composition_test engine/scene_composition_test.cc)
target_link_libraries(scene_composition_test scene_composition macros gtest_main)
gtest_discover_tests(scene_composition_test)

add_executable(parallax_layout_test engine/parallax_layout_test.cc)
target_link_libraries(parallax_layout_test parallax_layout gtest_main)
gtest_discover_tests(parallax_layout_test)

add_executable(simulation_pacer_test game/simulation_pacer_test.cc)
target_link_libraries(simulation_pacer_test simulation_pacer macros gtest_main)
gtest_discover_tests(simulation_pacer_test)

add_executable(game_engine_test game/game_engine_test.cc)
target_link_libraries(game_engine_test game_engine macros gtest_main)
gtest_discover_tests(game_engine_test)

# --- Level Manager ---
add_executable(level_manager_test resources/level_manager_test.cc)
target_link_libraries(level_manager_test level_manager macros gtest_main gmock)
gtest_discover_tests(level_manager_test PROPERTIES RESOURCE_LOCK level_manager_test_data)

add_executable(parallax_theme_manager_test resources/parallax_theme_manager_test.cc)
target_link_libraries(parallax_theme_manager_test
  parallax_theme_manager macros gtest_main nlohmann_json::nlohmann_json)
gtest_discover_tests(parallax_theme_manager_test)

# --- Asset references ---
add_executable(asset_references_test resources/asset_references_test.cc)
target_link_libraries(asset_references_test asset_references macros gtest_main gmock)
gtest_discover_tests(asset_references_test)

# --- Tileset Manager ---
add_executable(tileset_manager_test resources/tileset_manager_test.cc)
target_link_libraries(tileset_manager_test tileset_manager macros gtest_main gmock)
gtest_discover_tests(tileset_manager_test)

add_executable(terrain_recipe_manager_test resources/terrain_recipe_manager_test.cc)
target_link_libraries(terrain_recipe_manager_test
  terrain_recipe_manager
  macros
  gtest_main
  gmock
  nlohmann_json::nlohmann_json
)
gtest_discover_tests(terrain_recipe_manager_test)

add_executable(source_artwork_manager_test resources/source_artwork_manager_test.cc)
target_link_libraries(source_artwork_manager_test
  source_artwork_manager
  macros
  gtest_main
  gmock
  nlohmann_json::nlohmann_json
)
gtest_discover_tests(source_artwork_manager_test)

add_executable(prop_recipe_manager_test resources/prop_recipe_manager_test.cc)
target_link_libraries(prop_recipe_manager_test
  prop_recipe_manager
  macros
  gtest_main
  gmock
  nlohmann_json::nlohmann_json
)
gtest_discover_tests(prop_recipe_manager_test)

add_executable(parallax_artwork_recipe_manager_test
  resources/parallax_artwork_recipe_manager_test.cc)
target_link_libraries(parallax_artwork_recipe_manager_test
  parallax_artwork_recipe_manager
  macros
  gtest_main
  nlohmann_json::nlohmann_json
)
gtest_discover_tests(parallax_artwork_recipe_manager_test)

# --- Shipped assets ---
# Reads the repository's assets/ directly, so the path is baked in rather than
# depending on ctest's working directory.
add_executable(shipped_assets_test assets/shipped_assets_test.cc)
target_link_libraries(shipped_assets_test
  tileset_manager
  texture_manager
  level_manager
  parallax_theme_manager
  sprite_manager
  blueprint_manager
  collider_manager
  prop_recipe_manager
  parallax_artwork_recipe_manager
  source_artwork_manager
  terrain_brush
  terrain_mask
  macros
  gtest_main
)
target_compile_definitions(shipped_assets_test
  PRIVATE ZEBES_TEST_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
# For the fake texture store SpriteManager needs in place of a live SDL one.
target_include_directories(shipped_assets_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(shipped_assets_test)

add_subdirectory(editor)
add_subdirectory(artwork)
add_subdirectory(objects)
add_subdirectory(curation)
add_subdirectory(generation)
add_subdirectory(terrain)
add_subdirectory(tools)
add_subdirectory(authoring)
