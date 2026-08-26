#include "editor/editor_engine.h"

#include "SDL.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "common/imgui_wrapper.h"
#include "common/sdl_wrapper.h"
#include "common/status_macros.h"
#include "engine/input_manager.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "platform/sdl/sdl_input_source.h"
#include "platform/sdl/sdl_texture_store.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<EditorEngine>> EditorEngine::Create() {
  ASSIGN_OR_RETURN(EngineConfig config, EngineConfig::Create());
  auto engine = absl::WrapUnique(new EditorEngine(std::move(config)));
  RETURN_IF_ERROR(engine->Init());
  return engine;
}

EditorEngine::EditorEngine(EngineConfig config) : config_(std::move(config)) {}

absl::Status EditorEngine::Init() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    return absl::InternalError(absl::StrCat("SDL initialization failed: ", SDL_GetError()));
  }

  ASSIGN_OR_RETURN(sdl_, SdlWrapper::Create(config_.window));

  // The store owns the GPU textures, the manager owns the metadata naming
  // them; the split keeps SDL out of the manager's interface.
  texture_resources_ = std::make_unique<SdlTextureStore>(*sdl_);
  ASSIGN_OR_RETURN(assets_, AssetWorkspace::Create({
                                .config = &config_,
                                .texture_resources = texture_resources_.get(),
                                .asset_root = config_.paths.assets(),
                            }));

  imgui_wrapper_ = ImGuiWrapper::Create();

  // Translates SDL events into Zebes input types; nothing above sees SDL.
  sdl_input_source_ = std::make_unique<SdlInputSource>(*sdl_, *imgui_wrapper_);
  ASSIGN_OR_RETURN(input_manager_, InputManager::Create({.input_source = sdl_input_source_.get()}));

  gui_ = std::make_unique<Gui>();
  ASSIGN_OR_RETURN(ui_, EditorUi::Create(sdl_.get(), &assets_->api(), gui_.get()));

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForSDLRenderer(sdl_->GetWindow(), sdl_->GetRenderer());
  ImGui_ImplSDLRenderer2_Init(sdl_->GetRenderer());

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
  input_manager_->Update();
  if (input_manager_->QuitRequested()) {
    *done = true;
  }
}

void EditorEngine::RenderFrame() {
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  ui_->Render();

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
  // ImGui's backends hold the SDL renderer; they go before anything releases it.
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  ui_.reset();
  assets_.reset();
  texture_resources_.reset();
  input_manager_.reset();
  sdl_input_source_.reset();
  imgui_wrapper_.reset();

  sdl_.reset();  // Unique ptr will destroy Wrapper which destroys window/renderer

  SDL_Quit();
  LOG(INFO) << "Editor engine shut down";
}

}  // namespace zebes
