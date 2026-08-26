add_executable(openai_image_client_test openai_image_client_test.cc)
target_link_libraries(openai_image_client_test
  PRIVATE image_io macros openai_image_client status_macros nlohmann_json::nlohmann_json gtest_main)
target_include_directories(openai_image_client_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(openai_image_client_test)

add_executable(codex_app_server_transport_test codex_app_server_transport_test.cc)
target_link_libraries(codex_app_server_transport_test
  PRIVATE codex_app_server_transport macros absl::cleanup absl::status absl::time gtest_main)
target_include_directories(codex_app_server_transport_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(codex_app_server_transport_test)

add_executable(codex_app_server_protocol_test codex_app_server_protocol_test.cc)
target_link_libraries(codex_app_server_protocol_test
  PRIVATE codex_app_server_protocol macros absl::status nlohmann_json::nlohmann_json gtest_main)
target_include_directories(codex_app_server_protocol_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(codex_app_server_protocol_test)

add_executable(codex_image_client_test codex_image_client_test.cc)
target_link_libraries(codex_image_client_test
  PRIVATE codex_app_server_transport codex_image_client image_generation image_io macros
          status_macros absl::status absl::statusor nlohmann_json::nlohmann_json gtest_main)
target_include_directories(codex_image_client_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(codex_image_client_test)

add_executable(image_generation_engine_test image_generation_engine_test.cc)
target_link_libraries(image_generation_engine_test
  PRIVATE blocking_callback_thread engine_runner image_generation image_generation_engine macros
          status_macros gtest_main)
target_include_directories(image_generation_engine_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(image_generation_engine_test)

add_executable(image_generation_request_controller_test
  image_generation_request_controller_test.cc)
target_link_libraries(image_generation_request_controller_test
  PRIVATE image_generation_request_controller macros absl::status absl::statusor gtest_main)
target_include_directories(image_generation_request_controller_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(image_generation_request_controller_test)

add_executable(image_generation_service_test image_generation_service_test.cc)
target_link_libraries(image_generation_service_test
  PRIVATE image_generation image_generation_engine image_generation_service macros status_macros
          absl::status absl::statusor absl::strings absl::time gtest_main)
target_include_directories(image_generation_service_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(image_generation_service_test)

add_executable(image_generation_test image_generation_test.cc)
target_link_libraries(image_generation_test
  gtest_main macros curl_http_transport http_transport image_generation)
target_include_directories(image_generation_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(image_generation_test)

add_executable(headless_asset_generation_test headless_asset_generation_test.cc)
target_link_libraries(headless_asset_generation_test
  PRIVATE
  fake_image_generation_client
  generated_asset_candidate
  headless_asset_generation
  image_digest
  image_generation_service
  macros
  status_macros
  absl::status
  gmock
  gtest_main
  nlohmann_json::nlohmann_json
)
target_include_directories(headless_asset_generation_test PRIVATE ${CMAKE_SOURCE_DIR})
gtest_discover_tests(headless_asset_generation_test)
