cmake_policy(SET CMP0079 NEW)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

# Bring in the GoogleTest CMake module (required for discovery)
include(GoogleTest)

# Override output directory specifically for tests (as you had before)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

# Library for status macros
add_library(macros INTERFACE macros.h)
target_link_libraries(macros INTERFACE absl::statusor)

# --- Texture Manager ---
add_executable(texture_manager_test resources/texture_manager_test.cc)
target_link_libraries(texture_manager_test texture_manager macros gtest_main)
gtest_discover_tests(texture_manager_test) # <--- REGISTERS THE TEST

# --- Sprite Manager ---
add_executable(sprite_manager_test resources/sprite_manager_test.cc)
target_link_libraries(sprite_manager_test sprite_manager texture_manager macros gtest_main)
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

# --- Level Manager ---
add_executable(level_manager_test resources/level_manager_test.cc)
target_link_libraries(level_manager_test level_manager macros gtest_main gmock)
gtest_discover_tests(level_manager_test)

# --- Tileset Manager ---
add_executable(tileset_manager_test resources/tileset_manager_test.cc)
target_link_libraries(tileset_manager_test tileset_manager macros gtest_main gmock)
gtest_discover_tests(tileset_manager_test)

add_subdirectory(editor)
add_subdirectory(tools)
