#include "test_main.h"

#include <SDL.h>
#include <stdio.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"

// Define the interactive flag
ABSL_FLAG(bool, interactive, false,
          "If true, splits screen 50/50 and enables mouse control for debugging.");

// Flag to control ImGui logging
ABSL_FLAG(bool, imgui_verbose, true, "If true, enables ImGui Test Engine logging to TTY.");

namespace {

// Struct to hold all our subsystems so we can pass them around easily
struct TestContext {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  ImGuiTestEngine* engine = nullptr;
  bool is_interactive = false;
};

// ----------------------------------------------------------------------------
// 1. Initialization Helpers
// ----------------------------------------------------------------------------

bool InitSDL(TestContext* ctx) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    LOG(ERROR) << "SDL Error: " << SDL_GetError();
    return false;
  }

  // Setup window with HighDPI support
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  ctx->window = SDL_CreateWindow("ImGui Test Runner", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);

  ctx->renderer =
      SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

  if (ctx->renderer == nullptr) {
    LOG(ERROR) << "Error creating SDL_Renderer!";
    return false;
  }
  return true;
}

void InitImGui(TestContext* ctx) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Fixes for Test Engine interaction
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  SDL_ShowCursor(SDL_ENABLE);

  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForSDLRenderer(ctx->window, ctx->renderer);
  ImGui_ImplSDLRenderer2_Init(ctx->renderer);
}

void InitTestEngine(TestContext* ctx) {
  ctx->engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(ctx->engine);

  test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Debug;
  test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  test_io.ScreenCaptureFunc = nullptr;
  test_io.ScreenCaptureUserData = nullptr;
  test_io.ConfigLogToTTY = absl::GetFlag(FLAGS_imgui_verbose);

  ImGuiTestEngine_Start(ctx->engine, ImGui::GetCurrentContext());
}

// ----------------------------------------------------------------------------
// 2. Window Layout Helper (The Split Screen Logic)
// ----------------------------------------------------------------------------

void BeginAppWindow(bool is_interactive, AppRenderCallback app_render) {
  ImVec2 display_sz = ImGui::GetIO().DisplaySize;

  // Interactive Mode: App gets Left Half (50%), Test Engine gets Right Half
  // CI Mode: App gets Full Screen (100%)
  float width = is_interactive ? display_sz.x * 0.5f : display_sz.x;

  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width, display_sz.y), ImGuiCond_Always);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;

  // We name this "App Window" so tests can reliably find it
  if (ImGui::Begin(kAppWindowName, nullptr, flags)) {
    if (app_render) app_render();
  }
  ImGui::End();
}

void DrawTestEngine(TestContext* ctx) {
  if (ctx->is_interactive) {
    ImVec2 display_sz = ImGui::GetIO().DisplaySize;
    float start_x = display_sz.x * 0.5f;

    // Force Test Engine to the Right Half
    ImGui::SetNextWindowPos(ImVec2(start_x, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(start_x, display_sz.y), ImGuiCond_Always);

    ImGuiTestEngine_ShowTestEngineWindows(ctx->engine, nullptr);
  }
}

// ----------------------------------------------------------------------------
// 3. Cleanup Helper
// ----------------------------------------------------------------------------

void Shutdown(TestContext* ctx) {
  ImGuiTestEngine_Stop(ctx->engine);
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  ImGuiTestEngine_DestroyContext(ctx->engine);

  SDL_DestroyRenderer(ctx->renderer);
  SDL_DestroyWindow(ctx->window);
  SDL_Quit();
}

}  // namespace

// ----------------------------------------------------------------------------
// Main Entry Point
// ----------------------------------------------------------------------------

int RunTestApp(int argc, char** argv, TestRegistrationCallback register_tests,
               AppSetupCallback app_setup, AppRenderCallback app_render,
               AppShutdownCallback app_shutdown) {
  absl::ParseCommandLine(argc, argv);

  TestContext ctx;
  ctx.is_interactive = absl::GetFlag(FLAGS_interactive);

  // 1. Initialize Subsystems
  if (!InitSDL(&ctx)) return -1;
  InitImGui(&ctx);
  InitTestEngine(&ctx);

  // 2. User Setup
  if (app_setup) app_setup();
  if (register_tests) register_tests(ctx.engine);

  // 3. Auto-Queue Tests (Only in CI/Non-Interactive Mode)
  if (!ctx.is_interactive) {
    ImGuiTestEngine_QueueTests(ctx.engine, ImGuiTestGroup_Tests, nullptr);
  }

  // 4. Main Loop
  bool done = false;
  while (!done) {
    // Poll Events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) done = true;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(ctx.window))
        done = true;
    }

    // New Frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Layout & Render
    BeginAppWindow(ctx.is_interactive, app_render);  // Draws the App (Left)
    DrawTestEngine(&ctx);                            // Draws the Debugger (Right)

    // Rendering to Screen
    ImGui::Render();
    SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx.renderer);

    // Fix Retina/HighDPI Scaling
    ImGuiIO& io = ImGui::GetIO();
    SDL_RenderSetScale(ctx.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ctx.renderer);
    SDL_RenderPresent(ctx.renderer);

    // Test Engine Hook
    ImGuiTestEngine_PostSwap(ctx.engine);

    // Check Exit Condition
    if (!ctx.is_interactive && ImGuiTestEngine_IsTestQueueEmpty(ctx.engine)) {
      done = true;
    }
  }

  // 5. Calculate Results
  ImGuiTestEngineResultSummary summary;
  ImGuiTestEngine_GetResultSummary(ctx.engine, &summary);
  int test_failures = summary.CountTested - summary.CountSuccess;

  LOG(INFO) << absl::StrFormat("Tests: %d/%d passed (%d failed)\n", summary.CountSuccess,
                               summary.CountTested, test_failures);

  // 6. Shutdown
  if (app_shutdown) app_shutdown();
  Shutdown(&ctx);

  return (test_failures == 0) ? 0 : 1;
}