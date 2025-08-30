add_library(status_macros INTERFACE status_macros.h)
target_link_libraries(status_macros INTERFACE absl::status)
target_link_libraries(status_macros INTERFACE absl::statusor)

add_library(vector INTERFACE vector.h)
target_link_libraries(vector INTERFACE absl::strings)

add_library(common INTERFACE common.h)
target_link_libraries(common INTERFACE vector)
target_link_libraries(common INTERFACE config)

add_library(config config.cc)
target_link_libraries(config absl::log)
target_link_libraries(config absl::strings)
target_link_libraries(config absl::str_format)
target_link_libraries(config SDL2-static SDL2_image SDL2_ttf)
target_link_libraries(config nlohmann_json::nlohmann_json)

add_library(logging logging.cc)
target_link_libraries(logging absl::log)
target_link_libraries(logging absl::log_sink)
target_link_libraries(logging absl::flags)
target_link_libraries(logging absl::strings)
