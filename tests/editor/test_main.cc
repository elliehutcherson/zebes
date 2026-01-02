#include "test_main.h"

#include <SDL.h>
#include <stdio.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"

int RunTestApp(int argc, char** argv, TestRegistrationCallback register_tests,
               AppSetupCallback app_setup, AppRenderCallback app_render,
               AppShutdownCallback app_shutdown) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // Setup window
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  // Using a distinct window title could be useful, or passed in options.
  SDL_Window* window = SDL_CreateWindow("ImGui Test Runner", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (renderer == NULL) {
    SDL_Log("Error creating SDL_Renderer!");
    return -1;
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  // Setup Test Engine
  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(engine);
  // TODO: Make these configurable?
  test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
  test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  test_io.ScreenCaptureFunc = NULL;
  test_io.ScreenCaptureUserData = NULL;

  // Run user setup if provided.
  if (app_setup) {
    app_setup();
  }

  // Register tests
  if (register_tests) {
    register_tests(engine);
  }

  // Start engine
  ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
  ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, /*filter=*/NULL);

  // Main loop
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) done = true;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(window))
        done = true;
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGuiTestEngine_ShowTestEngineWindows(engine, NULL);

    // Render Application UI
    if (app_render) {
      app_render();
    }

    // Rendering
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    // Post-Swap hook
    ImGuiTestEngine_PostSwap(engine);

    // Check if tests are finished
    if (ImGuiTestEngine_IsTestQueueEmpty(engine)) {
      done = true;
    }
  }

  // Get result
  ImGuiTestEngineResultSummary summary;
  ImGuiTestEngine_GetResultSummary(engine, &summary);
  int test_failures = summary.CountTested - summary.CountSuccess;

  // Cleanup
  if (app_shutdown) {
    app_shutdown();
  }
  ImGuiTestEngine_Stop(engine);

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  ImGuiTestEngine_DestroyContext(engine);

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return (test_failures == 0) ? 0 : 1;
}
