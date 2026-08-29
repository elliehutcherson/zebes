add_library(sdl_input_source sdl_input_source.cc)
target_link_libraries(sdl_input_source
  PUBLIC input_types sdl_wrapper SDL2-static absl::any_invocable
)

add_library(sdl_texture_store sdl_texture_store.cc)
target_link_libraries(sdl_texture_store
  PUBLIC texture_resource_store
  PRIVATE sdl_wrapper SDL2-static absl::status absl::statusor absl::strings status_macros
)

add_library(sdl_game_renderer sdl_game_renderer.cc)
target_link_libraries(sdl_game_renderer
  PUBLIC
  game_renderer
  game_scene
  game_view
  sdl_wrapper
  SDL2-static
  absl::status
  absl::statusor
  PRIVATE
  parallax_layout
  scene_composition
  scene_types
  sdl_texture_store
  status_macros
  absl::cleanup
  absl::strings
)

add_library(sdl_subsystem sdl_subsystem.cc)
target_link_libraries(sdl_subsystem
  PUBLIC absl::statusor
  PRIVATE SDL2-static absl::status absl::strings
)

add_library(sdl_game_host sdl_game_host.cc)
target_link_libraries(sdl_game_host
  PUBLIC
  config
  game_renderer
  game_view
  input_types
  sdl_game_renderer
  sdl_input_source
  sdl_subsystem
  sdl_texture_store
  sdl_wrapper
  texture_resource_store
  absl::statusor
  PRIVATE
  status_macros
  absl::status
)
