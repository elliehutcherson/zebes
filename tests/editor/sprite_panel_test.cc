#include "editor/blueprint/sprite_panel.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"
#include "test_main.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class SpritePanelTestApp {
 public:
  SpritePanelTestApp() {
    api_ = std::make_unique<NiceMock<MockApi>>();

    // Setup initial sprites
    Sprite s1{.id = "s1", .name = "Grass"};
    sprites_ = {s1};

    ON_CALL(*api_, GetAllSprites()).WillByDefault([this]() { return sprites_; });
    ON_CALL(*api_, GetSprite("s1")).WillByDefault(Return(&sprites_[0]));
    ON_CALL(*api_, UpdateSprite(_)).WillByDefault([this](const Sprite& s) {
      for (auto& existing : sprites_) {
        if (existing.id == s.id) {
          existing = s;
          return absl::OkStatus();
        }
      }
      return absl::OkStatus();
    });

    auto panel_or = SpritePanel::Create(api_.get());
    assert(panel_or.ok());
    panel_ = std::move(*panel_or);
  }

  void Draw() { panel_->Render(); }

  MockApi* api() { return api_.get(); }
  SpritePanel* panel() { return panel_.get(); }
  const std::vector<Sprite>& sprites() const { return sprites_; }

 private:
  std::unique_ptr<MockApi> api_;
  std::unique_ptr<SpritePanel> panel_;
  std::vector<Sprite> sprites_;
};

// Global instance
std::unique_ptr<SpritePanelTestApp> g_app;

void AppSetup() { g_app = std::make_unique<SpritePanelTestApp>(); }

void RegisterTests(ImGuiTestEngine* engine) {
  IM_REGISTER_TEST(engine, "sprite_panel", "full_flow")->TestFunc = [](ImGuiTestContext* ctx) {
    auto& app = *g_app;

    // 1. Setup
    ctx->SetRef("Test Window");

    // 2. View List & Select
    // Verify "Grass" is present in the list and click it.
    ctx->ItemClick("**/Sprites/Grass");

    // 3. Verify Selection & Details
    // Checking internal state
    IM_CHECK_EQ(app.panel()->GetSprite()->id, "s1");

    // Verify UI elements in details view
    // Note: SpritePanel pushes ID "SpritePanel"
    // "Offset X" input should be visible.
    IM_CHECK_SILENT(ctx->ItemExists("**/Offset X"));

    // 4. Edit
    // Change Offset X to 10
    ctx->ItemInputValue("**/Offset X", 10);

    // Verify internal state updated immediately (assuming immediate mode GUI binding)
    IM_CHECK_EQ(app.panel()->GetSprite()->frames[0].offset_x, 10);

    // 5. Save
    // Mock API is setup to update the backing store in UpdateSprite.
    ctx->ItemClick("**/Save");

    // Verify the backing store (Source of Truth) is updated
    const auto& sprites = app.sprites();
    auto it =
        std::find_if(sprites.begin(), sprites.end(), [](const Sprite& s) { return s.id == "s1"; });
    IM_CHECK(it != sprites.end());
    IM_CHECK_EQ(it->frames[0].offset_x, 10);

    // 6. Attach/Detach

    // Currently attached? No.
    IM_CHECK(!app.panel()->IsAttached());

    // Detach from details view (clears editing sprite)
    ctx->ItemClick("**/Detach");
    IM_CHECK(app.panel()->GetSprite() == nullptr);

    // Select again (highlights in list)
    ctx->ItemClick("**/Sprites/Grass");

    // Click "Attach" button in List view to officially attach and edit
    ctx->ItemClick("**/Attach");

    // NOW we should be in Details view and attached.
    IM_CHECK(app.panel()->IsAttached());
    IM_CHECK_EQ(app.panel()->GetSprite()->id, "s1");

    // 7. Verify Detach again
    ctx->ItemClick("**/Detach");
    IM_CHECK(!app.panel()->IsAttached());
  };
}

void AppDraw() {
  if (g_app) {
    ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    g_app->Draw();
    ImGui::End();
  }
}

void AppShutdown() { g_app.reset(); }

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  return RunTestApp(argc, argv, zebes::RegisterTests, zebes::AppSetup, zebes::AppDraw,
                    zebes::AppShutdown);
}
