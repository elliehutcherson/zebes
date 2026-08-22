#include "editor/editor_engine.h"

#include "SDL.h"
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
#include "platform/sdl/sdl_input_source.h"
#include "platform/sdl/sdl_texture_store.h"
#include "resources/blueprint_manager.h"
#include "resources/level_manager.h"
#include "resources/texture_manager.h"

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
  ASSIGN_OR_RETURN(texture_manager_,
                   TextureManager::Create(texture_resources_.get(), config_.paths.assets()));
  RETURN_IF_ERROR(texture_manager_->LoadAllTextures());

  ASSIGN_OR_RETURN(sprite_manager_,
                   SpriteManager::Create(texture_manager_.get(), config_.paths.assets()));
  RETURN_IF_ERROR(sprite_manager_->LoadAllSprites());

  ASSIGN_OR_RETURN(collider_manager_, ColliderManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(collider_manager_->LoadAllColliders());

  ASSIGN_OR_RETURN(blueprint_manager_, BlueprintManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(blueprint_manager_->LoadAllBlueprints());

  // Levels reference sprites and colliders by ID, so the level manager needs no
  // asset managers of its own.
  ASSIGN_OR_RETURN(level_manager_, LevelManager::Create(config_.paths.assets()));

  RETURN_IF_ERROR(level_manager_->LoadAllLevels());

  ASSIGN_OR_RETURN(parallax_theme_manager_, ParallaxThemeManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(parallax_theme_manager_->LoadAllThemes());

  ASSIGN_OR_RETURN(tileset_manager_, TilesetManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(tileset_manager_->LoadAllTilesets());

  // Generated terrain renders artwork the level asks for, so the Level tab
  // needs recipes as much as the Terrain tab does.
  ASSIGN_OR_RETURN(terrain_recipe_manager_, TerrainRecipeManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(terrain_recipe_manager_->LoadAllRecipes());

  ASSIGN_OR_RETURN(source_artwork_manager_, SourceArtworkManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(source_artwork_manager_->LoadAllArtwork());

  ASSIGN_OR_RETURN(prop_recipe_manager_, PropRecipeManager::Create(config_.paths.assets()));
  RETURN_IF_ERROR(prop_recipe_manager_->LoadAllRecipes());

  imgui_wrapper_ = ImGuiWrapper::Create();

  // Translates SDL events into Zebes input types; nothing above sees SDL.
  sdl_input_source_ = std::make_unique<SdlInputSource>(*sdl_, *imgui_wrapper_);
  ASSIGN_OR_RETURN(input_manager_, InputManager::Create({.input_source = sdl_input_source_.get()}));

  Api::Options api_options = {
      .config = &config_,
      .texture_manager = texture_manager_.get(),
      .sprite_manager = sprite_manager_.get(),
      .collider_manager = collider_manager_.get(),
      .blueprint_manager = blueprint_manager_.get(),
      .level_manager = level_manager_.get(),
      .parallax_theme_manager = parallax_theme_manager_.get(),
      .tileset_manager = tileset_manager_.get(),
      .terrain_recipe_manager = terrain_recipe_manager_.get(),
      .source_artwork_manager = source_artwork_manager_.get(),
      .prop_recipe_manager = prop_recipe_manager_.get(),
  };
  ASSIGN_OR_RETURN(api_, Api::Create(api_options));

  gui_ = std::make_unique<Gui>();
  ASSIGN_OR_RETURN(ui_, EditorUi::Create(sdl_.get(), api_.get(), gui_.get()));

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
  api_.reset();
  prop_recipe_manager_.reset();
  source_artwork_manager_.reset();
  terrain_recipe_manager_.reset();
  tileset_manager_.reset();
  sprite_manager_.reset();
  texture_manager_.reset();
  texture_resources_.reset();
  collider_manager_.reset();
  level_manager_.reset();
  parallax_theme_manager_.reset();
  input_manager_.reset();
  sdl_input_source_.reset();
  imgui_wrapper_.reset();

  sdl_.reset();  // Unique ptr will destroy Wrapper which destroys window/renderer

  SDL_Quit();
  LOG(INFO) << "Editor engine shut down";
}

}  // namespace zebes
