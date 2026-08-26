add_library(headless_texture_store headless_texture_store.cc)
target_link_libraries(headless_texture_store
  PUBLIC texture_resource_store
  PRIVATE image_io status_macros absl::flat_hash_set absl::status absl::statusor absl::strings
)
