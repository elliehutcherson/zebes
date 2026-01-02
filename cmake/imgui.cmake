set(IMGUI_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include/imgui)
file(GLOB IMGUI_SOURCES ${IMGUI_INCLUDE_DIR}/*.cpp)
file(GLOB IMGUI_HEADERS ${IMGUI_INCLUDE_DIR}/*.h)

set(IMGUI_BACKENDS_DIR ${IMGUI_INCLUDE_DIR}/backends)
set(IMGUI_BACKEND_SOURCES
  ${IMGUI_BACKENDS_DIR}/imgui_impl_sdl2.cpp
  ${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer2.cpp)

set(IMGUI_BACKEND_HEADERS
  ${IMGUI_BACKENDS_DIR}/imgui_impl_sdl2.h
  ${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer2.h)

set(IMGUI_STDLIB_DIR ${IMGUI_INCLUDE_DIR}/misc/cpp)
set(IMGUI_STDLIB_SOURCES ${IMGUI_STDLIB_DIR}/imgui_stdlib.cpp)
set(IMGUI_STDLIB_HEADERS ${IMGUI_STDLIB_DIR}/imgui_stdlib.h)

set(IMGUI_FILE_DIALOG_DIR ${CMAKE_SOURCE_DIR}/include/ImGuiFileDialog)

set(IMGUI_FILE_DIALOG_SOURCES
  ${IMGUI_FILE_DIALOG_DIR}/ImGuiFileDialog.cpp)
set(IMGUI_FILE_DIALOG_HEADERS
  ${IMGUI_FILE_DIALOG_DIR}/ImGuiFileDialog.h
  ${IMGUI_FILE_DIALOG_DIR}/ImGuiFileDialogConfig.h)

# Common source lists defined above
set(IMGUI_ALL_SOURCES
  ${IMGUI_SOURCES}
  ${IMGUI_BACKEND_HEADERS} ${IMGUI_BACKEND_SOURCES}
  ${IMGUI_STDLIB_HEADERS} ${IMGUI_STDLIB_SOURCES}
  ${IMGUI_FILE_DIALOG_HEADERS} ${IMGUI_FILE_DIALOG_SOURCES}
)

# 1. Interface Library (Headers & Includes)
add_library(imgui_headers INTERFACE)
target_include_directories(imgui_headers INTERFACE
  ${IMGUI_INCLUDE_DIR}
  ${IMGUI_BACKENDS_DIR}
  ${IMGUI_FILE_DIALOG_DIR}
)

# 2. Clean Implementation (No Test Engine)
add_library(imgui_clean STATIC ${IMGUI_ALL_SOURCES})
target_link_libraries(imgui_clean PUBLIC imgui_headers SDL2-static)

# 3. Tested Implementation (With Test Engine Hooks)
add_library(imgui_tested STATIC ${IMGUI_ALL_SOURCES})
target_link_libraries(imgui_tested PUBLIC imgui_headers SDL2-static)
target_compile_definitions(imgui_tested PUBLIC IMGUI_ENABLE_TEST_ENGINE)

# Default 'imgui' alias points to clean version
add_library(imgui ALIAS imgui_clean)

# For compatibility, though we should prefer explicit targets
set(IMGUI_LIBRARIES imgui)
