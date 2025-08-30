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

set(IMGUI_FILE_DIALOG_DIR ${CMAKE_SOURCE_DIR}/include/ImGuiFileDialog)
    
set(IMGUI_FILE_DIALOG_SOURCES
    ${IMGUI_FILE_DIALOG_DIR}/ImGuiFileDialog.cpp)
set(IMGUI_FILE_DIALOG_HEADERS
    ${IMGUI_FILE_DIALOG_DIR}/ImGuiFileDialog.h
    ${IMGUI_FILE_DIALOG_DIR}/ImGuiFileDialogConfig.h)

add_library(imgui STATIC
    ${IMGUI_SOURCES} 
    ${IMGUI_BACKEND_HEADERS} ${IMGUI_BACKEND_SOURCES}
    ${IMGUI_FILE_DIALOG_HEADERS} ${IMGUI_FILE_DIALOG_SOURCES})

target_include_directories(imgui PUBLIC
    ${IMGUI_INCLUDE_DIR}
    ${IMGUI_BACKENDS_DIR}
    ${IMGUI_FILE_DIALOG_DIR})

target_link_libraries(imgui SDL2-static)

set(IMGUI_LIBRARIES imgui)