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
#include "editor/level_editor/parallax_panel.h"
#include "objects/texture.h"
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

class BasePanelScenario : public IPanelScenario {
 public:
  void Render() override {
    // Force the ImGui window to match the SDL window size/pos
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    // Optional: Remove resize handles and movement since it fills the screen
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar;  // Remove this if you still want the title

    // Push style to remove rounded corners (looks better when fullscreen)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (ImGui::Begin(GetTitle(), nullptr, window_flags)) {
      absl::Status status = RenderContent();
      if (!status.ok()) LOG(ERROR) << status;
    }
    ImGui::End();

    ImGui::PopStyleVar();
  }

 protected:
  virtual absl::Status RenderContent() = 0;
};

class LevelPanelScenario : public BasePanelScenario {
 public:
  static absl::StatusOr<std::unique_ptr<LevelPanelScenario>> Create() {
    auto scenario = std::unique_ptr<LevelPanelScenario>(new LevelPanelScenario());

    // 2. Create the Panel
    ASSIGN_OR_RETURN(scenario->panel_, LevelPanel::Create({.api = scenario->api_.get()}));

    return scenario;
  }

  const char* GetTitle() const override { return "Level Panel Viewer"; }

 protected:
  absl::Status RenderContent() override {
    if (!panel_) return absl::OkStatus();
    return panel_->Render(editing_level_).status();
  }

 private:
  LevelPanelScenario() {
    // Setup default mock behavior to prevent crashes
    ON_CALL(*api_, GetAllLevels).WillByDefault([this]() { return dummy_levels_; });
  }

  std::unique_ptr<zebes::MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  std::unique_ptr<zebes::LevelPanel> panel_;
  std::optional<zebes::Level> editing_level_;
  std::vector<zebes::Level> dummy_levels_;
};

class ParallaxPanelScenario : public BasePanelScenario {
 public:
  static absl::StatusOr<std::unique_ptr<ParallaxPanelScenario>> Create() {
    auto scenario = std::unique_ptr<ParallaxPanelScenario>(new ParallaxPanelScenario());

    // Create the Panel
    ASSIGN_OR_RETURN(scenario->panel_, ParallaxPanel::Create({.api = scenario->api_.get()}));

    // Setup dummy data
    scenario->editing_level_ = Level();
    scenario->editing_level_->parallax_layers = {
        {"Background", "sky.png", {0.1f, 0.1f}, true},
        {"Midground", "mountains.png", {0.5f, 0.5f}, false},
        {"Foreground", "trees.png", {1.0f, 1.0f}, true},
    };
    scenario->dummy_textures_ = DummyTextures();

    return scenario;
  }

  const char* GetTitle() const override { return "Parallax Panel Viewer"; }

 protected:
  absl::Status RenderContent() override {
    if (!panel_) return absl::OkStatus();
    return panel_->Render(*editing_level_).status();
  }

 private:
  ParallaxPanelScenario() {
    // Setup default mock behavior to prevent crashes
    ON_CALL(*api_, GetAllTextures).WillByDefault([this]() { return dummy_textures_; });
  }

  std::unique_ptr<zebes::MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  std::unique_ptr<zebes::ParallaxPanel> panel_;
  std::optional<zebes::Level> editing_level_;
  std::vector<Texture> dummy_textures_ = DummyTextures();
};

inline absl::StatusOr<std::unique_ptr<IPanelScenario>> CreateScenario(absl::string_view flag) {
  if (flag == "level_panel") {
    ASSIGN_OR_RETURN(std::unique_ptr<LevelPanelScenario> scenario, LevelPanelScenario::Create());
    return scenario;
  }

  if (flag == "parallax_panel") {
    ASSIGN_OR_RETURN(std::unique_ptr<ParallaxPanelScenario> scenario,
                     ParallaxPanelScenario::Create());
    return scenario;
  }

  return absl::NotFoundError("Unknown scenario flag");
}

}  // namespace zebes