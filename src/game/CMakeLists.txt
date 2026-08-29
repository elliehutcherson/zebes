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
  animation
  blueprint
  collider
  input_types
  level
  player_input
  tile_collision
  tile_movement
  tileset
  transform
  sprite
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

add_library(player_simulation player_simulation.cc)
target_link_libraries(player_simulation
  PUBLIC
  camera
  game_engine
  input_types
  runtime_world
  absl::status
  absl::statusor
  absl::time
  PRIVATE
  scene_types
  status_macros
)

add_library(game_scene game_scene.cc)
target_link_libraries(game_scene
  PUBLIC
  loaded_level_assets
  scene_composition
  absl::flat_hash_map
  PRIVATE
  level
  scene_types
  status_macros
  absl::status
  absl::statusor
  absl::strings
)

add_library(game_renderer INTERFACE game_renderer.h)
target_link_libraries(game_renderer
  INTERFACE
  game_scene
  absl::status
)

add_library(game_runtime game_runtime.cc)
target_link_libraries(game_runtime
  PUBLIC
  asset_workspace
  config
  game_engine
  game_renderer
  input_manager
  input_types
  loaded_level_assets
  player_simulation
  runtime_world
  texture_resource_store
  absl::status
  absl::statusor
  PRIVATE
  game_scene
  status_macros
)

add_executable(run_game main.cc)
target_link_libraries(run_game
  game_runtime
  sdl_game_host
  status_macros
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
)
add_dependencies(run_game copy_assets)
