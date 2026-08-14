#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "editor/gui_interface.h"
#include "objects/texture.h"

namespace zebes {

class ConfigEditor {
 public:
  static absl::StatusOr<std::unique_ptr<ConfigEditor>> Create(Api* api, SdlWrapper* sdl,
                                                              GuiInterface* gui);

  ~ConfigEditor() = default;

  void Render();

  const EngineConfig& GetEditorConfig() const { return local_config_; }

 private:
  ConfigEditor(Api* api, SdlWrapper* sdl, GuiInterface* gui);

  Api* api_;
  SdlWrapper* sdl_;
  GuiInterface* gui_;

  // Config editor state
  const EngineConfig& current_config_;
  EngineConfig local_config_;
  std::string window_title_buffer_;

  // The last failure, shown until dismissed or superseded. Pressing Save
  // Config and being told nothing when the write failed is what this is for.
  std::optional<std::string> error_message_;
};

}  // namespace zebes