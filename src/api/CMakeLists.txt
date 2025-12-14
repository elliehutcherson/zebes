add_library(api STATIC
  api.cc
  api.h
)

target_link_libraries(api
  absl::status
  absl::statusor
  absl::time
  common
  status_macros
  sprite
  sprite_manager
  texture_manager
)
