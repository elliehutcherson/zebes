add_library(simulation_pacer simulation_pacer.cc)
target_link_libraries(simulation_pacer
  PUBLIC
  absl::statusor
  absl::time
  PRIVATE
  absl::status
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
