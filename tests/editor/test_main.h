#ifndef TESTS_EDITOR_TEST_MAIN_H_
#define TESTS_EDITOR_TEST_MAIN_H_

#include <functional>

struct ImGuiTestEngine;

// Callback type for registering tests.
using TestRegistrationCallback = std::function<void(ImGuiTestEngine*)>;

// Callback type for additional setup before the main loop starts.
// Useful for initializing application logic under test.
using AppSetupCallback = std::function<void()>;

// Callback type for rendering the application UI.
using AppRenderCallback = std::function<void()>;

// Callback type for cleanup after the main loop ends.
using AppShutdownCallback = std::function<void()>;

// Runs the standard ImGui Test Engine application life cycle:
// 1. Init SDL & ImGui
// 2. Init ImGui Test Engine
// 3. User Setup Callback (optional)
// 4. Register Tests
// 5. Run Main Loop (calls app_render in the loop)
// 6. Cleanup & Return status (0 = pass, 1 = fail)
int RunTestApp(int argc, char** argv, TestRegistrationCallback register_tests,
               AppSetupCallback app_setup = nullptr, AppRenderCallback app_render = nullptr,
               AppShutdownCallback app_shutdown = nullptr);

#endif  // TESTS_EDITOR_TEST_MAIN_H_
