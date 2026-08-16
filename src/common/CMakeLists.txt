add_library(status_macros INTERFACE status_macros.h)
target_link_libraries(status_macros INTERFACE absl::status)
target_link_libraries(status_macros INTERFACE absl::statusor)

find_package(Threads REQUIRED)

add_library(background_task INTERFACE background_task.h)
target_link_libraries(background_task
  INTERFACE
  absl::status
  absl::statusor
  absl::strings
  status_macros
  Threads::Threads
)

add_library(vector INTERFACE vector.h)
target_link_libraries(vector INTERFACE absl::strings)

add_library(common INTERFACE common.h utils.h)
target_link_libraries(common INTERFACE vector)
target_link_libraries(common INTERFACE config)

add_library(config config.cc)
target_link_libraries(config absl::flags)
target_link_libraries(config absl::log)
target_link_libraries(config absl::status)
target_link_libraries(config absl::statusor)
target_link_libraries(config absl::strings)
target_link_libraries(config absl::str_format)
target_link_libraries(config game_view)
target_link_libraries(config nlohmann_json::nlohmann_json)

add_library(logging logging.cc)
target_link_libraries(logging absl::log)
target_link_libraries(logging absl::log_sink)
target_link_libraries(logging absl::flags)
target_link_libraries(logging absl::strings)

add_library(image_io image_io.cc)
target_link_libraries(image_io absl::status)
target_link_libraries(image_io absl::statusor)
target_link_libraries(image_io absl::span)
target_link_libraries(image_io absl::strings)
target_include_directories(image_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include/stb)

add_library(image_digest image_digest.cc)
target_link_libraries(image_digest
  PUBLIC
  image_io
  absl::statusor
  PRIVATE
  absl::status
)

add_library(sdl_wrapper sdl_wrapper.cc)
target_link_libraries(sdl_wrapper config)
target_link_libraries(sdl_wrapper absl::status)
target_link_libraries(sdl_wrapper absl::statusor)
target_link_libraries(sdl_wrapper absl::strings)
target_link_libraries(sdl_wrapper SDL2-static SDL2_image)

add_library(imgui_wrapper imgui_wrapper.cc)
target_link_libraries(imgui_wrapper SDL2-static)
target_link_libraries(imgui_wrapper imgui)
