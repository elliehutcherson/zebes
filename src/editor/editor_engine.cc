#include "editor/editor_engine.h"

#include "SDL.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<EditorEngine>> EditorEngine::Create() {
  ASSIGN_OR_RETURN(GameConfig config, GameConfig::Create());
  auto engine = absl::WrapUnique(new EditorEngine(std::move(config)));
  RETURN_IF_ERROR(engine->Init());
  return engine;
}

EditorEngine::EditorEngine(GameConfig config) : config_(std::move(config)) {}

absl::Status EditorEngine::Init() {
  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    return absl::InternalError(absl::StrCat("SDL initialization failed: ", SDL_GetError()));
  }

  // Create SDL Wrapper (Window & Renderer)
  ASSIGN_OR_RETURN(sdl_, SdlWrapper::Create(config_.window));

  // Create DB
  Db::Options db_options = {.db_path = config_.paths.database(),
                            .migration_path = config_.paths.migrations()};
  ASSIGN_OR_RETURN(db_, Db::Create(db_options));

  // Create API
  Api::Options api_options = {.config = &config_, .db = db_.get()};
  ASSIGN_OR_RETURN(api_, Api::Create(api_options));

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(sdl_->GetWindow(), sdl_->GetRenderer());
  ImGui_ImplSDLRenderer2_Init(sdl_->GetRenderer());

  // Create UI
  ui_ = std::make_unique<EditorUi>();
  ui_->SetSdlWrapper(sdl_.get());

  LOG(INFO) << "Editor engine initialized successfully";
  return absl::OkStatus();
}

absl::Status EditorEngine::Run() {
  bool done = false;
  while (!done) {
    HandleEvents(&done);
    RenderFrame();
  }
  return absl::OkStatus();
}

void EditorEngine::HandleEvents(bool* done) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type == SDL_QUIT) {
      *done = true;
    }
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
        event.window.windowID == SDL_GetWindowID(sdl_->GetWindow())) {
      *done = true;
    }
  }
}

void EditorEngine::RenderFrame() {
  // Start the Dear ImGui frame
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  // Render UI
  ui_->Render(api_.get());

  // Rendering
  ImGui::Render();
  ImGuiIO& io = ImGui::GetIO();
  SDL_RenderSetScale(sdl_->GetRenderer(), io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y);
  SDL_SetRenderDrawColor(sdl_->GetRenderer(), 0, 0, 0, 255);
  SDL_RenderClear(sdl_->GetRenderer());
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_->GetRenderer());
  SDL_RenderPresent(sdl_->GetRenderer());
}

void EditorEngine::Shutdown() {
  // Cleanup
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  sdl_.reset();  // Unique ptr will destroy Wrapper which destroys window/renderer

  SDL_Quit();
  LOG(INFO) << "Editor engine shut down";
}

}  // namespace zebes
