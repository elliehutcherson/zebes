#include "editor/editor_engine.h"

#include "SDL.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "common/config.h"
#include "common/imgui_wrapper.h"
#include "common/sdl_wrapper.h"
#include "common/status_macros.h"
#include "engine/input_manager.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "resources/blueprint_manager.h"
#include "resources/level_manager.h"
#include "resources/texture_manager.h"

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

  // Create Texture Manager
  ASSIGN_OR_RETURN(texture_manager_, TextureManager::Create(sdl_.get(), config_.paths.assets()));
  RETURN_IF_ERROR(texture_manager_->LoadAllTextures());

  // Create Sprite Manager
  ASSIGN_OR_RETURN(sprite_manager_,
                   SpriteManager::Create(texture_manager_.get(), config_.paths.assets()));
  RETURN_IF_ERROR(sprite_manager_->LoadAllSprites());

  // Create Collider Manager
  ASSIGN_OR_RETURN(collider_manager_, ColliderManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(collider_manager_->LoadAllColliders());

  // Create Blueprint Manager
  ASSIGN_OR_RETURN(blueprint_manager_, BlueprintManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(blueprint_manager_->LoadAllBlueprints());

  // Create Level Manager
  ASSIGN_OR_RETURN(
      level_manager_,
      LevelManager::Create(sprite_manager_.get(), collider_manager_.get(), config_.paths.assets()));

  RETURN_IF_ERROR(level_manager_->LoadAllLevels());

  // Create ImGui Wrapper
  imgui_wrapper_ = ImGuiWrapper::Create();

  // Create Input Manager
  ASSIGN_OR_RETURN(input_manager_, InputManager::Create({.sdl_wrapper = sdl_.get(),
                                                         .imgui_wrapper = imgui_wrapper_.get()}));

  // Create API
  Api::Options api_options = {
      .config = &config_,
      .texture_manager = texture_manager_.get(),
      .sprite_manager = sprite_manager_.get(),
      .collider_manager = collider_manager_.get(),
      .blueprint_manager = blueprint_manager_.get(),
      .level_manager = level_manager_.get(),
  };
  ASSIGN_OR_RETURN(api_, Api::Create(api_options));

  // Create UI
  ASSIGN_OR_RETURN(ui_, EditorUi::Create(sdl_.get(), api_.get()));

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
  // Start the Dear ImGui frame
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  // Render UI
  ui_->Render();

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

  ui_.reset();
  api_.reset();
  sprite_manager_.reset();
  texture_manager_.reset();
  collider_manager_.reset();
  level_manager_.reset();

  sdl_.reset();  // Unique ptr will destroy Wrapper which destroys window/renderer

  SDL_Quit();
  LOG(INFO) << "Editor engine shut down";
}

}  // namespace zebes
