#pragma once

#include <functional>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "editor/blueprint_editor/blueprint_editor.h"
#include "editor/config_editor/config_editor.h"
#include "editor/gui_interface.h"
#include "editor/image_generation/image_generation_service.h"
#include "editor/level_editor/level_editor.h"
#include "editor/parallax_theme_editor/parallax_theme_editor.h"
#include "editor/prop_artwork_editor/prop_artwork_editor.h"
#include "editor/sdl_preview_texture.h"
#include "editor/sprite_editor/sprite_editor.h"
#include "editor/terrain_editor/terrain_editor.h"
#include "editor/texture_editor/texture_editor.h"
#include "editor/tileset_editor/tileset_editor.h"

namespace zebes {

// Owns every editor tab and draws them into one window each frame.
//
// A tab whose render fails is logged and shown as an inline error; the other
// tabs draw as normal, so one broken panel does not take the editor down with
// it.
//
// Member declaration order is destruction order. Several preview textures and
// the generation service are declared before the editors holding pointers to
// them, each marked with a note; reordering them leaves a panel pointing at
// freed memory.
class EditorUi {
 public:
  static absl::StatusOr<std::unique_ptr<EditorUi>> Create(SdlWrapper* sdl, Api* api,
                                                          GuiInterface* gui);
  ~EditorUi() = default;

  void Render();

 private:
  // Renders a tab and reports failures without discarding editor state.
  void RenderTab(const char* name, const std::function<absl::Status()>& render_fn);
  void RenderTab(const char* name, const std::function<absl::Status()>& render_fn, bool select);
  absl::Status HandleLevelThemeRequest();
  explicit EditorUi(SdlWrapper* sdl, Api* api, GuiInterface* gui);

  absl::Status Init();

  // Dependencies are non-owning and must outlive EditorUi.
  SdlWrapper* sdl_;
  Api* api_;
  GuiInterface* gui_;
  std::unique_ptr<TextureEditor> texture_editor_;
  std::unique_ptr<ConfigEditor> config_editor_;
  std::unique_ptr<SpriteEditor> sprite_editor_;
  std::unique_ptr<BlueprintEditor> blueprint_editor_;
  // Declared before the editor that uses it so it outlives the panel holding
  // the pointer. Its own texture rather than a share of the terrain tab's: two
  // tabs writing one streaming texture would each see the other's artwork.
  std::unique_ptr<SdlPreviewTexture> terrain_ghost_;
  std::unique_ptr<LevelEditor> level_editor_;
  std::unique_ptr<ParallaxThemeEditor> parallax_theme_editor_;
  // Declared before the editor that uses it so it outlives the panel holding
  // the pointer.
  std::unique_ptr<SdlPreviewTexture> terrain_preview_;
  // Prop previews update independently of terrain previews; sharing one
  // streaming texture would make whichever tab rendered last own both images.
  std::unique_ptr<SdlPreviewTexture> prop_artwork_preview_;
  std::unique_ptr<TilesetEditor> tileset_editor_;
  std::unique_ptr<TerrainEditor> terrain_editor_;
  // Declared before the editor that submits to either service, so the editor
  // cancels its in-flight request while the selected engine is still running.
  // Destroying the services first would join their threads with a request
  // nobody has abandoned.
  std::unique_ptr<ImageGenerationService> codex_image_generation_;
  std::unique_ptr<ImageGenerationService> openai_image_generation_;
  std::unique_ptr<PropArtworkEditor> prop_artwork_editor_;

  bool show_debug_metrics_ = false;
  bool select_parallax_theme_tab_ = false;
};

}  // namespace zebes
