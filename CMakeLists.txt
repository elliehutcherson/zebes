# https://stackoverflow.com/questions/45933732/how-to-specify-a-compiler-in-cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(CMAKE_C_COMPILER /Library/Developer/CommandLineTools/usr/bin/clang)
  set(CMAKE_CXX_COMPILER /Library/Developer/CommandLineTools/usr/bin/clang++)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_C_COMPILER /usr/bin/clang)
  set(CMAKE_CXX_COMPILER /usr/bin/clang++)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

cmake_minimum_required(VERSION 3.10)
project(zebes VERSION 0.1.0)

# I'm not sure when this was added, or when it is required, but I'm keeping it here for reference.
# enable_language(OBJC)

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

# 2. Make the test target a "dummy" alias 
# This ensures that 'db_test' waits for 'copy_assets' to finish, 
# but doesn't run a second command.
add_custom_target(copy_assets_tests)
add_dependencies(copy_assets_tests copy_assets)

enable_testing()

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
set(ABSL_FIND_GOOGLETEST OFF)
set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
set(ABSL_BUILD_TEST_HELPERS ON)
set(ABSL_LOCAL_GOOGLETEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/googletest)

# ## All custom find cmake files live in the cmake directory
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
include(${CMAKE_MODULE_PATH}/imgui.cmake)
include(${CMAKE_MODULE_PATH}/sqlite3.cmake)

# ## Add all of the subdirectories
add_subdirectory(include/SDL)
add_subdirectory(include/SDL_image)
add_subdirectory(include/SDL_ttf)
add_subdirectory(include/googletest)
add_subdirectory(include/abseil-cpp)
add_subdirectory(include/nlohmann)

include_directories(${SDL_INCLUDE_DIRS})
include_directories(${SDLIMAGE_INCLUDE_DIRS})
include_directories(${SDLTTF_INCLUDE_DIRS})
include_directories(include/googletest)
include_directories(include/googletest/googlemock/include)
include_directories(include/googletest/googletest/include)
include_directories(include/abseil-cpp)
include_directories(include/nlohmann/include)

include_directories(src)
add_subdirectory(src)

include_directories(tests)
add_subdirectory(tests)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
