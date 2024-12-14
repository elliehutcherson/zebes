#include "game.h"

#include <memory>

#include "SDL.h"
#include "SDL_events.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "engine/collision_manager.h"
#include "engine/config.h"
#include "engine/controller.h"
#include "engine/object.h"
#include "engine/polygon.h"
#include "engine/tile_manager.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"

namespace zebes {

Game::Game(const GameConfig &config) : config_(config) {
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

  LOG(INFO) << "Zebes: Initializing camera...";
  Camera::Options camera_options = {.config = &config_, .renderer = renderer_};
  absl::StatusOr<std::unique_ptr<Camera>> camera =
      Camera::Create(camera_options);
  if (!camera.ok()) return camera.status();
  camera_ = std::move(*camera);

  LOG(INFO) << "Zebes: Initializing map...";
  map_ = std::make_unique<Map>(&config_, camera_.get());
  absl::Status result = map_->Init(renderer_);
  if (!result.ok()) return result;

  LOG(INFO) << "Zebes: Initializing sprite manager...";
  sprite_manager_ =
      std::make_unique<SpriteManager>(&config_, renderer_, camera_.get());
  result = sprite_manager_->Init();
  if (!result.ok()) return result;

  LOG(INFO) << "Zebes: Initializing object...";
  ObjectOptions object_options = {.config = &config_,
                                  .object_type = ObjectType::kTile,
                                  .vertices = {
                                      {.x = 300, .y = 700},
                                      {.x = 350, .y = 700},
                                      {.x = 325, .y = 750},
                                  }};
  absl::StatusOr<std::unique_ptr<Object>> object =
      Object::Create(object_options);
  if (!object.ok()) return object.status();

  object_ = std::move(*object);
  result = object_->AddPrimaryAxisIndex(0, AxisDirection::axis_left);
  if (!result.ok()) return result;

  if (config_.mode == GameConfig::Mode::kPlayerMode) {
    LOG(INFO) << "Zebes: Initializing player...";
    absl::StatusOr<std::unique_ptr<Player>> player =
        Player::Create(&config_, sprite_manager_.get());
    if (!player.ok()) return player.status();

    player_ = std::move(*player);
    focus_ = static_cast<Focus *>(player_.get());
    result = sprite_manager_->AddSpriteObject(player_->GetObject());
    if (!result.ok()) return result;

  } else if (config_.mode == GameConfig::Mode::kCreatorMode) {
    LOG(INFO) << "Zebes: Initializing creator...";
    Creator::Options options = {.config = &config_, .camera = camera_.get()};
    absl::StatusOr<std::unique_ptr<Creator>> creator = Creator::Create(options);
    if (!creator.ok()) return creator.status();

    creator_ = std::move(*creator);
    focus_ = static_cast<Focus *>(creator_.get());

  } else {
    return absl::InvalidArgumentError("Invalid game mode.");
  }

  LOG(INFO) << "Zebes: Initializing collision manager...";
  absl::StatusOr<std::unique_ptr<CollisionManager>> collision_manager =
      CollisionManager::Create(&config_, camera_.get());
  if (!collision_manager.ok()) return collision_manager.status();
  collision_manager_ = std::move(*collision_manager);
  result = collision_manager_->AddObject(object_.get());
  if (!result.ok()) return result;
  if (config_.mode == GameConfig::Mode::kPlayerMode) {
    result = collision_manager_->AddObject(player_->GetObject());
    if (!result.ok()) return result;
  }

  LOG(INFO) << "Zebes: Initializing texture manager...";
  TextureManager::Options texture_manager_options = {
      .config = &config_,
      .camera = camera_.get(),
      .renderer = renderer_,
  };
  absl::StatusOr<std::unique_ptr<TextureManager>> texture_manager =
      TextureManager::Create(texture_manager_options);
  if (!texture_manager.ok()) return texture_manager.status();
  texture_manager_ = std::move(*texture_manager);

  LOG(INFO) << "Zebes: Initializing tile manager...";
  struct TileManager::Options tile_manager_options = {
      .config = &config_,
      .texture_manager = texture_manager_.get(),
      .collision_manager = collision_manager_.get(),
  };
  absl::StatusOr<std::unique_ptr<TileManager>> tile_manager =
      TileManager::Create(tile_manager_options);
  if (!tile_manager.ok()) return tile_manager.status();
  tile_manager_ = std::move(*tile_manager);

  LOG(INFO) << "Zebes: Initializing controller...";
  Controller::Options controller_options = {
      .save_config = [this](const GameConfig &config) {
        if (config_.mode != GameConfig::Mode::kCreatorMode) {
          LOG(FATAL) << "Controller: Save config called in player mode.";
        }
        absl::Status save_status = creator_->SaveConfig(config);
        if (!save_status.ok()) {
          LOG(ERROR) << "Controller: Save config failed: " << save_status;
        }
      }};
  absl::StatusOr<std::unique_ptr<Controller>> controller =
      Controller::Create(std::move(controller_options));
  if (!controller.ok()) return controller.status();
  controller_ = std::move(*controller);

  LOG(INFO) << "Zebes: Initializing hud...";
  Hud::Options hud_options = {.config = &config_,
                              .focus = focus_,
                              .controller = controller_.get(),
                              .texture_manager = texture_manager_.get(),
                              .window = window_,
                              .renderer = renderer_};

  absl::StatusOr<std::unique_ptr<Hud>> maybe_hud =
      Hud::Create(std::move(hud_options));
  if (!maybe_hud.ok()) return maybe_hud.status();
  hud_ = std::move(*maybe_hud);

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
    ImGui_ImplSDL2_ProcessEvent(&event);
    controller_->HandleEvent(&event);
  }
}

void Game::Update() {
  controller_->Update();
  GameUpdate();
  if (AdvanceFrame()) {
    object_->PreUpdate();
    hud_->Update();
    focus_->Update(controller_->GetState());
    collision_manager_->Update();
    camera_->Update(focus_->x_center(), focus_->y_center());
  }
}

void Game::Render() {
  SDL_RenderClear(renderer_);
  map_->Render();
  sprite_manager_->Render();
  collision_manager_->Render();
  if (config_.mode == GameConfig::Mode::kCreatorMode) {
    camera_->RenderGrid();
    creator_->Render();
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

  ImGui_ImplSDL2_Shutdown();
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