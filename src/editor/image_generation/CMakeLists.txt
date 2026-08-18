add_library(image_generation image_generation.cc)
target_link_libraries(image_generation
  PUBLIC
  image_io
  absl::status
  absl::statusor
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
  PRIVATE
  absl::cleanup
  absl::strings
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
