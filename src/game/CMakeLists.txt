add_library(simulation_pacer simulation_pacer.cc)
target_link_libraries(simulation_pacer
  PUBLIC
  absl::statusor
  absl::time
  PRIVATE
  absl::status
  status_macros
)

add_library(game_engine game_engine.cc)
target_link_libraries(game_engine
  PUBLIC
  engine_contract
  simulation_pacer
  absl::status
  PRIVATE
  notification_set
  status_macros
  absl::statusor
  absl::time
)

add_library(player_input player_input.cc)
target_link_libraries(player_input
  PUBLIC
  input_types
)

add_library(runtime_world runtime_world.cc)
target_link_libraries(runtime_world
  PUBLIC
  collider
  input_types
  level
  player_input
  tile_collision
  transform
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  status_macros
  absl::check
  absl::status
  absl::strings
)

add_library(free_fly_simulation free_fly_simulation.cc)
target_link_libraries(free_fly_simulation
  PUBLIC
  camera
  camera_controller
  game_engine
  absl::status
  absl::statusor
  absl::time
  PRIVATE
  scene_types
  status_macros
)

add_library(game_level_assets game_level_assets.cc)
target_link_libraries(game_level_assets
  PUBLIC
  collider
  level
  parallax_theme
  sprite
  texture_handle
  tileset
  absl::statusor
  PRIVATE
  api
  status_macros
  absl::status
  absl::strings
)

add_library(game_scene game_scene.cc)
target_link_libraries(game_scene
  PUBLIC
  game_level_assets
  scene_composition
  PRIVATE
  level
  scene_types
  status_macros
  absl::status
  absl::statusor
  absl::strings
)

add_library(game_runtime game_runtime.cc)
target_link_libraries(game_runtime
  PUBLIC
  game_engine
  absl::status
  absl::statusor
  PRIVATE
  asset_workspace
  config
  free_fly_simulation
  game_level_assets
  game_scene
  input_manager
  sdl_game_renderer
  sdl_input_source
  sdl_texture_store
  sdl_wrapper
  status_macros
  SDL2-static
  absl::strings
)

add_executable(run_game main.cc)
target_link_libraries(run_game
  game_runtime
  status_macros
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
)
add_dependencies(run_game copy_assets)
