set(IMGUI_TEST_ENGINE_DIR ${CMAKE_SOURCE_DIR}/include/imgui_test_engine/imgui_test_engine)
file(GLOB IMGUI_TEST_ENGINE_SOURCES ${IMGUI_TEST_ENGINE_DIR}/*.cpp)
file(GLOB IMGUI_TEST_ENGINE_HEADERS ${IMGUI_TEST_ENGINE_DIR}/*.h)

add_library(imgui_test_engine STATIC
  ${IMGUI_TEST_ENGINE_SOURCES}
  ${IMGUI_TEST_ENGINE_HEADERS}
)

target_include_directories(imgui_test_engine PUBLIC
  ${IMGUI_TEST_ENGINE_DIR}
  ${CMAKE_SOURCE_DIR}/include/imgui_test_engine
)

target_link_libraries(imgui_test_engine PUBLIC imgui_tested)
target_compile_definitions(imgui_test_engine PUBLIC IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)


# Test Suite
set(IMGUI_TEST_SUITE_DIR ${CMAKE_SOURCE_DIR}/include/imgui_test_engine/imgui_test_suite)
file(GLOB IMGUI_TEST_SUITE_SOURCES ${IMGUI_TEST_SUITE_DIR}/*.cpp)
list(FILTER IMGUI_TEST_SUITE_SOURCES EXCLUDE REGEX "imgui_tests_docking.cpp")
file(GLOB IMGUI_TEST_SUITE_HEADERS ${IMGUI_TEST_SUITE_DIR}/*.h)

add_library(imgui_test_suite STATIC
  ${IMGUI_TEST_SUITE_SOURCES}
  ${IMGUI_TEST_SUITE_HEADERS}
)

target_include_directories(imgui_test_suite PUBLIC
  ${IMGUI_TEST_SUITE_DIR}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(imgui_test_suite PRIVATE -fno-char8_t)
endif()

target_compile_definitions(imgui_test_suite PRIVATE IMGUI_BROKEN_TESTS=0)

target_link_libraries(imgui_test_suite PUBLIC imgui_test_engine)
