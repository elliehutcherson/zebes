#include <cstring>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "editor/gui.h"
#include "editor/parallax_artwork_editor/parallax_artwork_editor.h"
#include "editor/preview_texture_sink.h"
#include "gmock/gmock.h"
#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "test_main.h"
#include "tests/api_mock.h"

namespace zebes {

class ParallaxArtworkEditorTestPeer {
 public:
  static const ParallaxArtworkEditorModel& Model(const ParallaxArtworkEditor& editor) {
    return editor.model_;
  }
};

namespace {

class StubPreviewSink final : public PreviewTextureSink {
 public:
  absl::StatusOr<ImTextureID> Upload(const RgbaImage&) override {
    return absl::FailedPreconditionError("the empty artwork workspace has no preview to upload");
  }
};

struct ParallaxArtworkUiVars {
  testing::NiceMock<MockApi> api;
  Gui gui;
  StubPreviewSink preview;
  ImageGenerationProviderRegistry providers;
  absl::Status render_status;
  std::unique_ptr<ParallaxArtworkEditor> editor;
};

void RegisterTests(ImGuiTestEngine* engine) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "parallax_artwork", "select_fit_inside_framing");
  test->SetVarsDataType<ParallaxArtworkUiVars>();
  test->GuiFunc = [](ImGuiTestContext* context) {
    ParallaxArtworkUiVars& vars = context->GetVars<ParallaxArtworkUiVars>();
    if (vars.editor == nullptr) {
      absl::StatusOr<std::unique_ptr<ParallaxArtworkEditor>> editor =
          ParallaxArtworkEditor::Create({
              .api = &vars.api,
              .gui = &vars.gui,
              .preview = &vars.preview,
              .generation_providers = &vars.providers,
          });
      IM_CHECK(editor.ok());
      if (editor.ok()) vars.editor = *std::move(editor);
    }
    ImGui::SetNextWindowSize({1280.0F, 720.0F}, ImGuiCond_Always);
    ImGui::Begin("Parallax Artwork UI Test", nullptr, ImGuiWindowFlags_NoSavedSettings);
    if (vars.editor != nullptr) vars.render_status = vars.editor->Render();
    ImGui::End();
  };
  test->TestFunc = [](ImGuiTestContext* context) {
    ParallaxArtworkUiVars& vars = context->GetVars<ParallaxArtworkUiVars>();
    context->Yield();
    IM_CHECK(vars.render_status.ok());
    IM_CHECK(vars.editor != nullptr);
    if (vars.editor == nullptr) return;

    ImGuiWindow* input_window = nullptr;
    for (ImGuiWindow* window : context->UiContext->Windows) {
      if (std::strstr(window->Name, "/ParallaxArtworkInput_") == nullptr) continue;
      input_window = window;
      break;
    }
    IM_CHECK(input_window != nullptr);
    if (input_window == nullptr) return;

    ImGuiTestItemList items;
    context->GatherItems(&items, input_window->ID);
    const ImGuiID framing_id = input_window->GetID("Framing##ParallaxArtwork");
    const ImGuiTestItemInfo* framing = items.GetByID(framing_id);
    IM_CHECK(framing != nullptr);
    if (framing == nullptr) return;
    IM_CHECK((framing->ItemFlags & ImGuiItemFlags_Disabled) == 0);
    IM_CHECK_GT(framing->RectClipped.GetWidth(), 0.0F);
    IM_CHECK_GT(framing->RectClipped.GetHeight(), 0.0F);

    context->ItemClick(framing_id);
    context->SetRef("//$FOCUSED");
    context->ItemClick("Fit inside");

    const ParallaxArtworkEditorModel& model = ParallaxArtworkEditorTestPeer::Model(*vars.editor);
    IM_CHECK_EQ(model.settings().pipeline.frame_policy, ParallaxArtworkFramePolicy::kFitInside);
  };
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) { return RunTestApp(argc, argv, zebes::RegisterTests); }
