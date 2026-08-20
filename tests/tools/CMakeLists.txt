# Binaries here are run by hand, never by CTest or scripts/test.sh. The runner
# in scripts/affected_tests.py selects any executable whose artifact lands under
# bin/tests and scripts/test.sh then executes each one, so a manually-driven
# binary must land somewhere else or it becomes part of the automated sweep.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tools)

add_executable(panel_viewer panel_viewer.cc)
target_link_libraries(panel_viewer
  level_panel
  editor_gui
  ${IMGUI_LIBRARIES}
  SDL2-static
  absl::flags
  absl::flags_parse
  absl::log
  absl::check
  absl::status
  absl::statusor
)
target_include_directories(panel_viewer PRIVATE ${CMAKE_SOURCE_DIR})

# Opt-in live provider test. No gtest_discover_tests call: registering it would
# put a credential-dependent, billable request in the merge gate, which cannot
# pass on a fork and should not pass on money.
add_executable(openai_image_client_live_test openai_image_client_live_test.cc)
target_link_libraries(openai_image_client_live_test
  PRIVATE
  credential_source
  curl_http_transport
  image_generation
  image_io
  macros
  openai_image_client
  status_macros
  absl::status
  absl::statusor
  absl::time
  gtest_main
)
