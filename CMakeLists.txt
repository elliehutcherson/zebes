# https://stackoverflow.com/questions/45933732/how-to-specify-a-compiler-in-cmake
set(CMAKE_C_COMPILER /Library/Developer/CommandLineTools/usr/bin/clang)
set(CMAKE_CXX_COMPILER /Library/Developer/CommandLineTools/usr/bin/clang++)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 
set(CMAKE_BUILD_TYPE Debug)

cmake_minimum_required(VERSION 3.0.0)
project(zebes VERSION 0.1.0)
enable_language(OBJC)


### Require out-of-source builds
file(TO_CMAKE_PATH ${PROJECT_BINARY_DIR}/CMakeLists.txt LOC_PATH)
if(EXISTS ${LOC_PATH})
    message(FATAL_ERROR "You cannot build in a source directory.")
endif()

### Copy assets to build directory
file(COPY ${CMAKE_CURRENT_LIST_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/src)
add_custom_target(copy_assets 
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/src/assets
)
add_dependencies(copy_assets ${PROJECT_NAME})

file(COPY ${CMAKE_CURRENT_LIST_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests)
add_custom_target(copy_assets_tests
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/tests/assets
)
add_dependencies(copy_assets_tests ${PROJECT_NAME})

enable_testing()

# https://stackoverflow.com/questions/75551185/cmake-using-sdl2-and-sdl2-image-via-fetchcontent
# This is only way I have found to get SDL_image to build with SDL2 with a static import
# When linking SDL_image, cmake can not find the target SDL_image-static, you must use SDL_image.
set(BUILD_SHARED_LIBS FALSE)
set(SDL2IMAGE_INSTALL OFF)
set(SDL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/SDL/include/SDL2)
# there is no include directory? SDL_image.h is in the main folder.
set(SDLIMAGE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/SDL_image)
# there is no include directory? SDL_ttf.h is in the main folder.
set(SDLTTF_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/SDL_ttf)

# Find SQLite3
find_package(SQLite3)

### Add all of the subdirectories
add_subdirectory(include/SDL)
add_subdirectory(include/SDL_image)
add_subdirectory(include/SDL_ttf)
add_subdirectory(include/abseil-cpp)
add_subdirectory(include/googletest)
add_subdirectory(include/nlohmann)

include_directories(${SDL_INCLUDE_DIRS})
include_directories(${SDLIMAGE_INCLUDE_DIRS})
include_directories(${SDLTTF_INCLUDE_DIRS})
include_directories(${SQLite3_INCLUDE_DIRS})
include_directories(include/abseil-cpp)
include_directories(include/googletest)
include_directories(include/nlohmann)

### All custom find cmake files live in the cmake directory
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
include(${CMAKE_MODULE_PATH}/imgui.cmake)

include_directories(src)
add_subdirectory(src)

include_directories(tests)
add_subdirectory(tests)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)