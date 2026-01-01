add_library(vec INTERFACE)
target_include_directories(vec INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(sprite INTERFACE)
target_include_directories(sprite INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(texture INTERFACE)
target_include_directories(texture INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(collider INTERFACE)
target_include_directories(collider INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(collider INTERFACE
  vec
)

add_library(transform INTERFACE)
target_include_directories(transform INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(transform INTERFACE
  vec
)

add_library(blueprint INTERFACE)
target_include_directories(blueprint INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(blueprint INTERFACE)

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

add_library(level INTERFACE)
target_include_directories(level INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(level INTERFACE
  entity
  vec
  absl::flat_hash_map
)
