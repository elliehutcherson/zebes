add_library(vec INTERFACE)
target_include_directories(vec INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(game_view INTERFACE)
target_include_directories(game_view INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(sprite INTERFACE)
target_include_directories(sprite INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(camera INTERFACE)
target_include_directories(camera INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(camera INTERFACE
  vec
)

add_library(texture INTERFACE)
target_include_directories(texture INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(collider INTERFACE)
target_include_directories(collider INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(collider INTERFACE
  vec
  absl::strings
)

add_library(transform INTERFACE)
target_include_directories(transform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(transform INTERFACE
  vec
)

add_library(blueprint INTERFACE)
target_include_directories(blueprint INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(blueprint INTERFACE)

add_library(tileset INTERFACE)
target_include_directories(tileset INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tileset INTERFACE
  absl::hash
  absl::strings
)

add_library(tile_shape_geometry INTERFACE)
target_include_directories(tile_shape_geometry INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tile_shape_geometry INTERFACE
  tileset
  absl::span
)

add_library(body INTERFACE)
target_include_directories(body INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(body INTERFACE
  vec
)

add_library(entity INTERFACE)
target_include_directories(entity INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(entity INTERFACE
  body
  collider
  sprite
  transform
)

add_library(entity_factory entity_factory.cc)
target_include_directories(entity_factory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(entity_factory
  PUBLIC
  blueprint
  entity
  vec
  absl::statusor
  PRIVATE
  absl::status
  absl::strings
)

add_library(level level.cc)
target_include_directories(level PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(level
  PUBLIC
  entity
  vec
  absl::flat_hash_map
  absl::status
  absl::statusor
  absl::strings
  PRIVATE
  absl::flat_hash_set
  status_macros
)

add_library(parallax_theme parallax_theme.cc)
target_include_directories(parallax_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(parallax_theme
  PUBLIC
  vec
  absl::status
  absl::statusor
  nlohmann_json::nlohmann_json
  PRIVATE
  status_macros
  absl::strings
)
