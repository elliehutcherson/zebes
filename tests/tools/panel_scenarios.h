#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/status_macros.h"
#include "gmock/gmock.h"
#include "imgui.h"

// Project Headers
#include "editor/level_editor/level_panel.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

using ::testing::NiceMock;
using ::testing::ReturnRef;

}  // namespace

class IPanelScenario {
 public:
  virtual ~IPanelScenario() = default;
  virtual void Render() = 0;
  virtual const char* GetTitle() const = 0;
};

class LevelPanelScenario : public IPanelScenario {
 public:
  static absl::StatusOr<std::unique_ptr<LevelPanelScenario>> Create() {
    auto scenario = std::unique_ptr<LevelPanelScenario>(new LevelPanelScenario());

    // 2. Create the Panel
    ASSIGN_OR_RETURN(scenario->panel_, LevelPanel::Create({.api = scenario->api_.get()}));

    return scenario;
  }

  void Render() override {
    if (!panel_) return;

    // NEW CODE: Force the ImGui window to match the SDL window size/pos
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    // Optional: Remove resize handles and movement since it fills the screen
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar;  // Remove this if you still want the title

    // Push style to remove rounded corners (looks better when fullscreen)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (ImGui::Begin("Mock Level Editor", nullptr, window_flags)) {
      absl::StatusOr<LevelResult> result = panel_->Render(editing_level_);
      if (!result.ok()) LOG(ERROR) << result.status();
    }
    ImGui::End();

    ImGui::PopStyleVar();  // Don't forget to pop the style var!
  }

  const char* GetTitle() const override { return "Level Panel Viewer"; }

 private:
  LevelPanelScenario() {
    // Setup default mock behavior to prevent crashes
    ON_CALL(*api_, GetAllLevels).WillByDefault([this]() {
      std::vector<Level> levels;
      // for (const Level& l : dummy_levels_) {
      //   levels.push_back(l.GetCopy());
      // }
      return levels;
    });
  }

  std::unique_ptr<zebes::MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  std::unique_ptr<zebes::LevelPanel> panel_;
  std::optional<zebes::Level> editing_level_;
  std::vector<zebes::Level> dummy_levels_;
};

inline absl::StatusOr<std::unique_ptr<IPanelScenario>> CreateScenario(absl::string_view flag) {
  if (flag == "level_panel") {
    ASSIGN_OR_RETURN(std::unique_ptr<LevelPanelScenario> scenario, LevelPanelScenario::Create());
    return scenario;
  }

  return absl::NotFoundError("Unknown scenario flag");
}

}  // namespace zebes