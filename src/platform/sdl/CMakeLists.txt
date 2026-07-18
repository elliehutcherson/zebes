add_library(sdl_input_source sdl_input_source.cc)
target_link_libraries(sdl_input_source
  PUBLIC input_types
  PRIVATE sdl_wrapper imgui_wrapper SDL2-static
)

add_library(sdl_texture_store sdl_texture_store.cc)
target_link_libraries(sdl_texture_store
  PUBLIC texture_resource_store
  PRIVATE sdl_wrapper SDL2-static absl::status absl::statusor absl::strings
)
