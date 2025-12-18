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
