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
#include "editor/level_editor/level_editor.h"
#include "editor/sprite_editor/sprite_editor.h"
#include "editor/texture_editor/texture_editor.h"
#include "editor/tileset_editor/tileset_editor.h"

namespace zebes {

class EditorUi {
 public:
  static absl::StatusOr<std::unique_ptr<EditorUi>> Create(SdlWrapper* sdl, Api* api,
                                                          GuiInterface* gui);
  ~EditorUi() = default;

  // Render all editor UI windows
  void Render();

 private:
  // Renders a tab and reports failures without discarding editor state.
  void RenderTab(const char* name, const std::function<absl::Status()>& render_fn);
  explicit EditorUi(SdlWrapper* sdl, Api* api, GuiInterface* gui);

  // Initialize owned objects.
  absl::Status Init();

  // Dependencies are non-owning and must outlive EditorUi.
  SdlWrapper* sdl_;
  Api* api_;
  GuiInterface* gui_;
  std::unique_ptr<TextureEditor> texture_editor_;
  std::unique_ptr<ConfigEditor> config_editor_;
  std::unique_ptr<SpriteEditor> sprite_editor_;
  std::unique_ptr<BlueprintEditor> blueprint_editor_;
  std::unique_ptr<LevelEditor> level_editor_;
  std::unique_ptr<TilesetEditor> tileset_editor_;

  // Debug state
  bool show_debug_metrics_ = false;
};

}  // namespace zebes
