#pragma once

#include <memory>

#include "SDL.h"
#include "absl/status/status.h"
#include "api/api.h"
#include "common/config.h"
#include "common/imgui_wrapper.h"
#include "common/sdl_wrapper.h"
#include "editor/editor_ui.h"
#include "editor/gui.h"
#include "engine/input_manager.h"
#include "platform/sdl/sdl_input_source.h"
#include "platform/sdl/sdl_texture_store.h"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/level_manager.h"
#include "resources/prop_recipe_manager.h"
#include "resources/source_artwork_manager.h"
#include "resources/sprite_manager.h"
#include "resources/terrain_recipe_manager.h"
#include "resources/texture_manager.h"
#include "resources/tileset_manager.h"

namespace zebes {

// The editor's composition root. Owns every subsystem and wires them together
// here, which is why nothing below needs a global or a service locator: each
// manager is handed its dependencies as raw pointers, and this object outlives
// all of them.
//
// Create() runs Init(), so a returned engine is fully initialized or the call
// failed. Init() builds subsystems in dependency order and loads every asset
// definition, failing on the first error rather than starting the editor with
// assets missing.
//
// Shutdown() releases ImGui and SDL and must run before the engine is
// destroyed. Member declaration order is destruction order, and the UI and API
// are declared after the managers they point into.
class EditorEngine {
 public:
  static absl::StatusOr<std::unique_ptr<EditorEngine>> Create();

  ~EditorEngine() = default;
  EditorEngine(const EditorEngine&) = delete;
  EditorEngine& operator=(const EditorEngine&) = delete;

  EditorEngine(EditorEngine&& other) = default;
  EditorEngine& operator=(EditorEngine&& other) = default;

  absl::Status Init();

  // Pumps input and renders until the window is closed.
  absl::Status Run();

  void Shutdown();

 private:
  explicit EditorEngine(EngineConfig config);

  void HandleEvents(bool* done);

  void RenderFrame();

  // Declaration order is destruction order. Everything below borrows from
  // something above it, so this sequence is load-bearing, not stylistic.
  std::unique_ptr<SdlWrapper> sdl_;

  EngineConfig config_;
  std::unique_ptr<SdlTextureStore> texture_resources_;
  std::unique_ptr<TextureManager> texture_manager_;
  std::unique_ptr<SpriteManager> sprite_manager_;
  std::unique_ptr<ColliderManager> collider_manager_;
  std::unique_ptr<BlueprintManager> blueprint_manager_;
  std::unique_ptr<LevelManager> level_manager_;
  std::unique_ptr<TilesetManager> tileset_manager_;
  std::unique_ptr<TerrainRecipeManager> terrain_recipe_manager_;
  std::unique_ptr<SourceArtworkManager> source_artwork_manager_;
  std::unique_ptr<PropRecipeManager> prop_recipe_manager_;
  std::unique_ptr<ImGuiWrapper> imgui_wrapper_;
  std::unique_ptr<SdlInputSource> sdl_input_source_;
  std::unique_ptr<InputManager> input_manager_;
  std::unique_ptr<Api> api_;

  std::unique_ptr<Gui> gui_;
  std::unique_ptr<EditorUi> ui_;
};

}  // namespace zebes
