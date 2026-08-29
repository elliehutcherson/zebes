add_library(sdl_input_source sdl_input_source.cc)
target_link_libraries(sdl_input_source
  PUBLIC input_types absl::any_invocable
  PRIVATE sdl_wrapper SDL2-static
)

add_library(sdl_texture_store sdl_texture_store.cc)
target_link_libraries(sdl_texture_store
  PUBLIC texture_resource_store
  PRIVATE sdl_wrapper SDL2-static absl::status absl::statusor absl::strings status_macros
)

add_library(sdl_game_renderer sdl_game_renderer.cc)
target_link_libraries(sdl_game_renderer
  PUBLIC
  game_scene
  game_view
  absl::status
  absl::statusor
  PRIVATE
  parallax_layout
  scene_composition
  scene_types
  sdl_texture_store
  sdl_wrapper
  status_macros
  SDL2-static
  absl::cleanup
  absl::strings
)
