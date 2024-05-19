#include "game.h"

#include <iostream>
#include <memory>

#include "SDL.h"

#include "SDL_events.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "config.h"
#include "controller.h"
#include "object.h"
#include "polygon.h"

namespace zebes {

Game::Game(const GameConfig &config) : config_(config) {
  std::cout << "Zebes: Game Constructing..." << std::endl;
}

absl::Status Game::Init() {
  std::cout << "Zebes: Initializing..." << std::endl;
  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    std::cout << "Zebes: Not Initializing..." << std::endl;
    return absl::OkStatus();
  }

  std::cout << "Zebes: Initializing window..." << std::endl;
  window_ = SDL_CreateWindow(config_.window.title.c_str(), config_.window.xpos,
    config_.window.ypos, config_.window.width, config_.window.height, config_.window.flags);
  if (window_ == nullptr) {
    return absl::AbortedError("Failed to create window.");
  }

  std::cout << "Zebes: Initializing font library..." << std::endl;
  if (TTF_Init() != 0) {
    return absl::InternalError(
      absl::StrFormat("SDL_ttf could not initialize! %s", TTF_GetError()));
  }

  std::cout << "Zebes: Initializing renderer..." << std::endl;
  renderer_ = SDL_CreateRenderer(window_, -1, 0);
  if (renderer_ == nullptr) {
    return absl::AbortedError("Failed to create renderer.");
  }

  std::cout << "Zebes: Initializing camera..." << std::endl;
  camera_ = std::make_unique<Camera>(&config_, renderer_);
  absl::Status result = camera_->Init();
  if (!result.ok()) return result;

  std::cout << "Zebes: Initializing map..." << std::endl;
  map_ = std::make_unique<Map>(&config_, camera_.get());
  result = map_->Init(renderer_);
  if (!result.ok()) return result;

  std::cout << "Zebes: Initializing sprite manager..." << std::endl;
  sprite_manager_ = std::make_unique<SpriteManager>(&config_, renderer_, camera_.get());
  result = sprite_manager_->Init();
  if (!result.ok()) return result;

  std::cout << "Zebes: Initializing tile matrix..." << std::endl;
  tile_matrix_ = std::make_unique<TileMatrix>(&config_);
  result = tile_matrix_->Init();
  if (!result.ok()) return result;

  std::cout << "Zebes: Initializing object..." << std::endl;
  ObjectOptions object_options = {
    .config = &config_,
    .camera = camera_.get(),
    .object_type = ObjectType::kTile,
    .vertices = {
      {.x = 300, .y = 700},
      {.x = 350, .y = 700},
      {.x = 325, .y = 750},
    }
  };
  object_= std::make_unique<Object>(object_options);
  result = object_->AddPrimaryAxisIndex(0, AxisDirection::axis_left);
  if (!result.ok()) return result;

  if (config_.mode == GameConfig::Mode::kPlayerMode) {
    std::cout << "Zebes: Initializing player..." << std::endl;
    absl::StatusOr<std::unique_ptr<Player>> maybe_player = 
      Player::Create(&config_, sprite_manager_.get());
    if (!maybe_player.ok()) return maybe_player.status();
    player_ = std::move(*maybe_player);
    focus_ = static_cast<Focus*>(player_.get());
    result = sprite_manager_->AddSpriteObject(player_->GetObject());
    if (!result.ok()) return result;
  } else if (config_.mode == GameConfig::Mode::kCreatorMode) {
    std::cout << "Zebes: Initializing creator..." << std::endl;
    creator_ = std::make_unique<Creator>(&config_);
    focus_ = static_cast<Focus*>(creator_.get());
  } else {
    return absl::InvalidArgumentError("Invalid game mode.");
  }

  std::cout << "Zebes: Initializing collision manager..." << std::endl;
  collision_manager_ = std::make_unique<CollisionManager>(&config_);
  result = collision_manager_->Init();
  if (!result.ok()) return result;
  result = collision_manager_->AddObject(object_.get());
  if (!result.ok()) return result;
  if (config_.mode == GameConfig::Mode::kPlayerMode) {
    result = collision_manager_->AddObject(player_->GetObject());
    if (!result.ok()) return result;
  }

  std::cout << "Zebes: Initializing tile manager..." << std::endl;
  tile_manager_ = std::make_unique<TileManager>(&config_, camera_.get(), tile_matrix_.get());
  result = tile_manager_->Init(collision_manager_.get(), sprite_manager_.get());
  if (!result.ok()) return result;

  std::cout << "Zebes: Initializing hud..." << std::endl;
  absl::StatusOr<std::unique_ptr<Hud>> maybe_hud = 
      Hud::Create(&config_, focus_, window_, renderer_);
  if (!maybe_hud.ok()) return maybe_hud.status();
  hud_ = std::move(*maybe_hud);

  std::cout << "Zebes: Initializing controller..." << std::endl;
  controller_ = std::make_unique<Controller>(&config_);

  std::cout << "Zebes: Initialization done..." << std::endl;
  is_running_ = true;
  return absl::OkStatus();
}

absl::Status Game::Run() {
  std::cout << "Zebes: Running..." << std::endl;
  while (is_running_) {
    PrePipeline();
    HandleEvent();
    Update();
    Render();
    Clear();
    GameDelay();
  }
  Clean();
  return absl::OkStatus();
}

void Game::PrePipeline() {
  frame_start_ = SDL_GetTicks64();
}

void Game::HandleEvent() {
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
    focus_->Update(controller_->GetState());
    collision_manager_->Update();
    camera_->Update(focus_->x_center(), focus_->y_center());
  }
}

void Game::Render() {
  SDL_RenderClear(renderer_);
  map_->Render();
  object_->Render();
  sprite_manager_->Render();
  if (config_.mode == GameConfig::Mode::kCreatorMode) camera_->RenderGrid();
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
  std::cout << "Zebes: Exiting..." << std::endl;

  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyWindow(window_);
  SDL_DestroyRenderer(renderer_);
  SDL_Quit();
  std::cout << "Zebes: Game Cleaned..." << std::endl;
}

bool Game::IsRunning() {
  return is_running_;
}

bool Game::AdvanceFrame() {
  return !enable_frame_by_frame_ || advance_frame_by_frame_;
}

void Game::GameUpdate() {
  const ControllerState* controller_state = controller_->GetState();
  if (controller_state->game_quit != KeyState::none)
    is_running_ = false;
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