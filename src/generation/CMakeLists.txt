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

add_library(artwork_generation_prompts artwork_generation_prompts.cc)

add_library(generated_asset_candidate generated_asset_candidate.cc)
target_link_libraries(generated_asset_candidate
  PUBLIC
  parallax_artwork_recipe
  prepare_parallax_artwork_asset
  prepare_prop_asset
  prop_recipe
  source_artwork
  image_io
  nlohmann_json::nlohmann_json
  absl::status
  absl::statusor
  PRIVATE
  image_digest
  status_macros
  absl::strings
)

add_library(fake_image_generation_client fake_image_generation_client.cc)
target_link_libraries(fake_image_generation_client
  PUBLIC image_generation
  PRIVATE image_io absl::statusor
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

add_library(codex_app_server_transport codex_app_server_transport.cc)
target_link_libraries(codex_app_server_transport
  PUBLIC
  absl::status
  absl::statusor
  PRIVATE
  absl::cleanup
  absl::strings
  status_macros
)

add_library(codex_app_server_protocol codex_app_server_protocol.cc)
target_link_libraries(codex_app_server_protocol
  PUBLIC
  absl::flat_hash_map
  absl::statusor
  PRIVATE
  absl::status
  absl::strings
  nlohmann_json::nlohmann_json
  status_macros
)

add_library(codex_image_client codex_image_client.cc)
target_link_libraries(codex_image_client
  PUBLIC
  codex_app_server_transport
  image_generation
  absl::statusor
  absl::time
  PRIVATE
  codex_app_server_protocol
  image_io
  absl::cleanup
  absl::flat_hash_map
  absl::status
  absl::strings
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

add_library(image_generation_request_controller image_generation_request_controller.cc)
target_link_libraries(image_generation_request_controller
  PUBLIC
  image_generation
  image_generation_engine
  absl::status
  absl::statusor
  PRIVATE
  absl::log
  absl::memory
  status_macros
  utc_timestamp
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

add_library(image_generation_service image_generation_service.cc)
target_link_libraries(image_generation_service
  PUBLIC
  blocking_callback_thread
  codex_image_client
  credential_source
  engine_runner
  http_transport
  image_generation
  image_generation_engine
  openai_image_client
  absl::statusor
  PRIVATE
  curl_http_transport
  absl::log
  absl::memory
  absl::status
  status_macros
)

add_library(headless_asset_generation headless_asset_generation.cc)
target_link_libraries(headless_asset_generation
  PUBLIC
  api
  image_generation_service
  absl::statusor
  PRIVATE
  artwork_generation_prompts
  atomic_directory_publisher
  generated_artwork_postprocessor
  generated_asset_candidate
  image_digest
  image_generation
  image_generation_engine
  image_io
  parallax_artwork_recipe
  prop_recipe
  status_macros
  utc_timestamp
  absl::status
  absl::str_format
  absl::strings
  absl::time
  nlohmann_json::nlohmann_json
)
