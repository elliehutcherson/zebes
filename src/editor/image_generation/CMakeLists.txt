add_library(image_generation image_generation.cc)
target_link_libraries(image_generation
  PUBLIC
  image_io
  absl::status
  absl::statusor
  absl::time
  PRIVATE
  absl::cleanup
  absl::strings
  status_macros
)

add_library(credential_source credential_source.cc)
target_link_libraries(credential_source
  PUBLIC
  absl::statusor
  absl::strings
  PRIVATE
  absl::status
)

add_library(http_transport http_transport.cc)
target_link_libraries(http_transport
  PUBLIC
  credential_source
  absl::status
  absl::statusor
  absl::time
  PRIVATE
  absl::cleanup
  absl::strings
  status_macros
)

add_library(openai_image_client openai_image_client.cc)
target_link_libraries(openai_image_client
  PUBLIC
  credential_source
  http_transport
  image_generation
  absl::statusor
  PRIVATE
  absl::status
  absl::strings
  absl::time
  image_io
  nlohmann_json::nlohmann_json
  status_macros
)

add_library(image_generation_engine image_generation_engine.cc)
target_link_libraries(image_generation_engine
  PUBLIC
  engine_contract
  image_generation
  mpsc_queue
  notification_set
  absl::flat_hash_map
  absl::status
  absl::statusor
  PRIVATE
  absl::check
  absl::time
  status_macros
)

add_library(curl_http_transport curl_http_transport.cc)
target_link_libraries(curl_http_transport
  PUBLIC
  http_transport
  PRIVATE
  CURL::libcurl
  absl::status
  absl::statusor
  absl::strings
  status_macros
)
