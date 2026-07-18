cmake_minimum_required(VERSION 3.21)

project(zebes VERSION 0.1.0)

include(CTest)

option(BUILD_UI_TESTING "Build display-dependent SDL/ImGui integration tests" OFF)
if(BUILD_UI_TESTING AND NOT BUILD_TESTING)
  message(FATAL_ERROR "BUILD_UI_TESTING requires BUILD_TESTING=ON")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ## Require out-of-source builds
file(TO_CMAKE_PATH ${PROJECT_BINARY_DIR}/CMakeLists.txt LOC_PATH)

if(EXISTS ${LOC_PATH})
  message(FATAL_ERROR "You cannot build in a source directory.")
endif()

# ## Symlink assets to build directory
add_custom_target(copy_assets
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E create_symlink
  ${CMAKE_SOURCE_DIR}/assets
  ${CMAKE_BINARY_DIR}/bin/assets
  COMMENT "Symlinking assets to build/bin/assets"
)

# https://stackoverflow.com/questions/75551185/cmake-using-sdl2-and-sdl2-image-via-fetchcontent
# This is only way I have found to get SDL_image to build with SDL2 with a static import
# When linking SDL_image, cmake can not find the target SDL_image-static, you must use SDL_image.
set(BUILD_SHARED_LIBS FALSE)
set(SDL2IMAGE_INSTALL OFF)
set(SDL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include/SDL/include)
set(SDLIMAGE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include/SDL_image)

# TESTONLY: When added, this target will only be built if both
# BUILD_TESTING=ON and ABSL_BUILD_TESTING=ON.
# Jesus Christ... this was a pain...
if(BUILD_TESTING)
  set(ABSL_FIND_GOOGLETEST OFF)
  set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
  set(ABSL_BUILD_TEST_HELPERS ON)
  set(ABSL_LOCAL_GOOGLETEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/googletest)
endif()

# ## All custom find cmake files live in the cmake directory
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
include(${CMAKE_MODULE_PATH}/imgui.cmake)
include(${CMAKE_MODULE_PATH}/sqlite3.cmake)

if(BUILD_UI_TESTING)
  include(${CMAKE_MODULE_PATH}/imgui_test_engine.cmake)
endif()

# ## Add all of the subdirectories
add_subdirectory(include/SDL)
add_subdirectory(include/SDL_image)
add_subdirectory(include/SDL_ttf)

if(BUILD_TESTING)
  add_subdirectory(include/googletest)
endif()

add_subdirectory(include/abseil-cpp)
add_subdirectory(include/nlohmann)

include_directories(${SDL_INCLUDE_DIRS})
include_directories(${SDLIMAGE_INCLUDE_DIRS})
include_directories(${SDLTTF_INCLUDE_DIRS})
include_directories(include/abseil-cpp)
include_directories(include/nlohmann/include)

if(BUILD_TESTING)
  include_directories(include/googletest)
  include_directories(include/googletest/googlemock/include)
  include_directories(include/googletest/googletest/include)
endif()

include_directories(src)
add_subdirectory(src)

if(BUILD_TESTING)
  include_directories(tests)
  add_subdirectory(tests)
endif()

add_subdirectory(scripts)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
