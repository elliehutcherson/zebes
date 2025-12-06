#include "engine/game.h"

#include <memory>

#include "SDL.h"
#include "SDL_events.h"
#include "SDL_video.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "api/api.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "db/db.h"
#include "engine/camera.h"
#include "engine/collision_manager.h"
#include "engine/controller.h"
#include "engine/sprite_manager.h"
#include "hud/hud.h"

namespace zebes {

Game::Game(GameConfig config) : config_(std::move(config)) {
  LOG(INFO) << "Zebes: Game Constructing...";
}

absl::Status Game::Init() {
  LOG(INFO) << "Zebes: Initializing...";
  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    LOG(INFO) << "Zebes: Not Initializing...";
    return absl::OkStatus();
  }

  LOG(INFO) << "Zebes: Initializing window...";
  window_ = SDL_CreateWindow(config_.window.title.c_str(), config_.window.xpos,
                             config_.window.ypos, config_.window.width,
                             config_.window.height, config_.window.flags);
  if (window_ == nullptr) {
    return absl::AbortedError("Failed to create window.");
  }
  SDL_GetWindowSize(window_, &config_.window.width, &config_.window.height);
  LOG(INFO) << "window width: " << config_.window.width
            << " window height: " << config_.window.height;

  LOG(INFO) << "Zebes: Initializing font library...";
  if (TTF_Init() != 0) {
    return absl::InternalError(
        absl::StrFormat("SDL_ttf could not initialize! %s", TTF_GetError()));
  }

  LOG(INFO) << "Zebes: Initializing renderer...";
  renderer_ = SDL_CreateRenderer(window_, -1, 0);
  if (renderer_ == nullptr) {
    return absl::AbortedError("Failed to create renderer.");
  }

  LOG(INFO) << "Zebes: Initializing focus lite...";
  focus_lite_ = std::make_unique<FocusLite>();
  focus_ = focus_lite_.get();

  LOG(INFO) << "Zebes: Initializing camera...";
  Camera::Options camera_options = {.config = &config_, .renderer = renderer_};
  ASSIGN_OR_RETURN(camera_, Camera::Create(camera_options));

  LOG(INFO) << "Zebes: Initializing database...";
  Db::Options db_options = {.db_path = kZebesDatabasePath};
  ASSIGN_OR_RETURN(db_, Db::Create(db_options));

  LOG(INFO) << "Zebes: Initializing sprite manager...";
  SpriteManager::Options sprite_manager_options = {
      .config = &config_,
      .renderer = renderer_,
      .camera = camera_.get(),
      .db = db_.get(),
  };
  ASSIGN_OR_RETURN(sprite_manager_,
                   SpriteManager::Create(sprite_manager_options));

  LOG(INFO) << "Zebes: Initializing collision manager...";
  ASSIGN_OR_RETURN(collision_manager_,
                   CollisionManager::Create(&config_, camera_.get()));

  LOG(INFO) << "Zebes: Initializing controller...";
  ASSIGN_OR_RETURN(
      controller_,
      Controller::Create({.save_config = [](const GameConfig &config) {
        SaveConfig(config);
      }}));

  LOG(INFO) << "Zebes: Initializing api...";
  Api::Options api_options = {.config = &config_,
                              .db = db_.get()};
  ASSIGN_OR_RETURN(api_, Api::Create(api_options));

  LOG(INFO) << "Zebes: Initializing hud...";
  Hud::Options hud_options = {.config = &config_,
                              .focus = focus_,
                              .controller = controller_.get(),
                              .sprite_manager = sprite_manager_.get(),
                              .api = api_.get(),
                              .window = window_,
                              .renderer = renderer_};

  ASSIGN_OR_RETURN(hud_, Hud::Create(std::move(hud_options)));

  LOG(INFO) << "Zebes: Initialization done...";
  is_running_ = true;
  return absl::OkStatus();
}

absl::Status Game::Run() {
  LOG(INFO) << "Zebes: Running...";
  while (is_running_) {
    PrePipeline();
    InjectEvents();
    HandleEvents();
    Update();
    Render();
    Clear();
    GameDelay();
  }
  Clean();
  return absl::OkStatus();
}

void Game::PrePipeline() { frame_start_ = SDL_GetTicks64(); }

void Game::InjectEvents() { hud_->InjectEvents(); }

void Game::HandleEvents() {
  controller_->HandleInternalEvents();
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    // ImGui_ImplSDL2_ProcessEvent(&event);
    hud_->HandleEvent(event);
    controller_->HandleEvent(&event);
  }
}

void Game::Update() {
  controller_->Update();
  GameUpdate();
  if (AdvanceFrame()) {
    hud_->Update();
    focus_->Update(controller_->GetState());
    collision_manager_->Update();
    camera_->Update(focus_->x_center(), focus_->y_center());
  }
}

void Game::Render() {
  SDL_RenderClear(renderer_);
  collision_manager_->Render();
  if (config_.mode == GameConfig::Mode::kCreatorMode) {
    camera_->RenderGrid();
  }
  hud_->Render();
  SDL_RenderPresent(renderer_);
}

void Game::Clear() {
  controller_->Clear();
  if (AdvanceFrame()) {
    collision_manager_->CleanUp();
  }
}

void Game::Clean() {
  LOG(INFO) << "Zebes: Exiting...";

  // ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyWindow(window_);
  SDL_DestroyRenderer(renderer_);
  SDL_Quit();
  LOG(INFO) << "Zebes: Game Cleaned...";
}

bool Game::IsRunning() { return is_running_; }

bool Game::AdvanceFrame() {
  return !enable_frame_by_frame_ || advance_frame_by_frame_;
}

void Game::GameUpdate() {
  const ControllerState *controller_state = controller_->GetState();
  if (controller_state->game_quit != KeyState::none) is_running_ = false;
  if (controller_state->enable_frame_by_frame == KeyState::down)
    enable_frame_by_frame_ = !enable_frame_by_frame_;
  if (controller_state->advance_frame == KeyState::down) {
    advance_frame_by_frame_ = true;
  } else {
    advance_frame_by_frame_ = false;
  }
}

void Game::GameDelay() {
  frame_time_ = SDL_GetTicks64() - frame_start_;
  if (config_.frame_delay > frame_time_)
    SDL_Delay(config_.frame_delay - frame_time_);
}

}  // namespace zebes