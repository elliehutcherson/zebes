#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/status_macros.h"
#include "imgui.h"

// Project Headers
#include "editor/gui.h"
#include "editor/level_editor/level_panel.h"

namespace zebes {

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

    ASSIGN_OR_RETURN(scenario->panel_, LevelPanel::Create(&scenario->gui_));

    return scenario;
  }

  const char* GetTitle() const override { return "Level Panel Viewer"; }

 protected:
  absl::Status RenderContent() override {
    if (!panel_) return absl::OkStatus();
    absl::StatusOr<LevelPanelEvent> event = model_.has_active_level()
                                                ? panel_->RenderDetails(model_)
                                                : panel_->RenderList(model_);
    if (!event.ok()) return event.status();

    if (event->action == LevelPanelAction::kCreate) {
      RETURN_IF_ERROR(model_.FinishCreate("panel-viewer-level"));
    } else if (event->action == LevelPanelAction::kDelete) {
      model_.FinishDelete();
    }
    return absl::OkStatus();
  }

 private:
  LevelPanelScenario() = default;

  Gui gui_;
  std::unique_ptr<zebes::LevelPanel> panel_;
  LevelPanelModel model_;
};

inline absl::StatusOr<std::unique_ptr<IPanelScenario>> CreateScenario(absl::string_view flag) {
  if (flag == "level_panel") {
    ASSIGN_OR_RETURN(std::unique_ptr<LevelPanelScenario> scenario, LevelPanelScenario::Create());
    return std::unique_ptr<IPanelScenario>(std::move(scenario));
  }

  return absl::NotFoundError("Unknown scenario flag");
}

}  // namespace zebes
