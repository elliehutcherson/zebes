#pragma once

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/parallax_theme_editor/parallax_theme_editor_model.h"
#include "editor/texture_preview.h"

namespace zebes {

class ParallaxThemeEditor {
 public:
  static absl::StatusOr<std::unique_ptr<ParallaxThemeEditor>> Create(Api* api, GuiInterface* gui);

  absl::Status Render();
  absl::Status OpenTheme(const std::string& theme_id);

 private:
  ParallaxThemeEditor(Api* api, GuiInterface* gui);
  absl::Status Save();
  absl::Status Duplicate();
  void SetError(const absl::Status& status);

  Api* api_;
  GuiInterface* gui_;
  TexturePreviewRenderer texture_preview_;
  ParallaxThemeEditorModel model_;
  ConfirmPrompt delete_prompt_;
  std::optional<std::string> error_;
};

}  // namespace zebes
