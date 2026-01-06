#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ImGuiFileDialog.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"
#include "objects/texture.h"

namespace zebes {

class ConfigEditor {
 public:
  static absl::StatusOr<std::unique_ptr<ConfigEditor>> Create(Api* api, SdlWrapper* sdl);

  ~ConfigEditor() = default;

  void Render();

  const EngineConfig& GetEditorConfig() const { return local_config_; }

 private:
  ConfigEditor(Api* api, SdlWrapper* sdl);

  Api* api_;
  SdlWrapper* sdl_;

  // Config editor state
  const EngineConfig& current_config_;
  EngineConfig local_config_;
  std::string window_title_buffer_;
};

}  // namespace zebes