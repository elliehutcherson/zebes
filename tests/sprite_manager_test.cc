#include <iostream>
#include <memory>

#include "SDL_render.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "engine/camera.h"
#include "engine/game.h"
#include "engine/object.h"
#include "engine/sprite_manager.h"

bool IsRunning() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      return false;
    }
  }
  return true;
}

absl::Status Run() {
  std::cout << "Zebes: Initializing config..." << std::endl;
  zebes::GameConfig config = zebes::GameConfig::Create();

  std::cout << "Zebes: Initializing window..." << std::endl;
  // Create window with graphics context
  SDL_Window *window = SDL_CreateWindow(
      "title", 0, 0, 800, 400, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    return absl::AbortedError("Failed to create window.");
  }

  std::cout << "Zebes: Initializing font library..." << std::endl;
  if (TTF_Init() != 0) {
    return absl::InternalError(
        absl::StrFormat("SDL_ttf could not initialize! %s", TTF_GetError()));
  }

  std::cout << "Zebes: Initializing renderer..." << std::endl;
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  if (renderer == nullptr) {
    return absl::AbortedError("Failed to create renderer.");
  }

  std::cout << "Zebes: Initializing camera..." << std::endl;
  std::unique_ptr<zebes::Camera> camera =
      std::make_unique<zebes::Camera>(&config, renderer);
  absl::Status result = camera->Init();
  if (!result.ok())
    return result;

  std::cout << "Zebes: Initializing sprite manager..." << std::endl;
  std::unique_ptr<zebes::SpriteManager> sprite_manager =
      std::make_unique<zebes::SpriteManager>(&config, renderer, camera.get());
  result = sprite_manager->Init();
  if (!result.ok())
    return result;

  std::vector<zebes::Point> vertices = {
      {.x = 0, .y = 0},
      {.x = 32, .y = 0},
      {.x = 32, .y = 32},
      {.x = 0, .y = 32},
  };

  zebes::ObjectOptions options{
      .config = &config,
      .camera = camera.get(),
      .object_type = zebes::ObjectType::kTile,
      .vertices = vertices,
  };

  absl::StatusOr<std::unique_ptr<zebes::SpriteObject>> sprite_object_result =
      zebes::SpriteObject::Create(options);
  if (!sprite_object_result.ok())
    return sprite_object_result.status();
  std::unique_ptr<zebes::SpriteObject> sprite_object =
      std::move(*sprite_object_result);

  result = sprite_object->AddSpriteProfile({.type = zebes::SpriteType::kDirt1});
  if (!result.ok())
    return result;

  result = sprite_manager->AddSpriteObject(sprite_object.get());
  if (!result.ok())
    return result;

  bool is_running = true;
  do {
    is_running = IsRunning();
    sprite_manager->Render();
  } while (is_running);

  SDL_DestroyWindow(window);
  return absl::OkStatus();
}

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::Status result = Run();
  if (!result.ok()) {
    std::cout << "Failed to run Zebes: " << result.ToString() << std::endl;
    return 1;
  }
  return 0;
}