#include "editor/blueprint_editor/sprite_panel.h"

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "editor/gui.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "test_main.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

using ::testing::Return;

void RegisterTests(ImGuiTestEngine* engine) {
  // Test 1: Attach with invalid ID
  {
    ImGuiTest* t = IM_REGISTER_TEST(engine, "sprite_panel", "attach_invalid_id");
    t->TestFunc = [](ImGuiTestContext* ctx) {
      MockApi mock_api;
      EXPECT_CALL(mock_api, GetAllSprites()).WillRepeatedly(Return(std::vector<Sprite>{}));

      Gui gui;
      auto panel_or_error = SpritePanel::Create(&mock_api, &gui);
      IM_CHECK(panel_or_error.ok());
      auto panel = std::move(panel_or_error.value());

      EXPECT_CALL(mock_api, GetSprite("invalid_id"))
          .WillOnce(Return(absl::NotFoundError("Sprite not found")));

      absl::Status status = panel->Attach("invalid_id");
      IM_CHECK_EQ(status.code(), absl::StatusCode::kNotFound);

      // Verify we are effectively not attached (should render list)
      int initial_list_count = panel->GetCounters().render_list;
      auto result_or_error = panel->Render();
      IM_CHECK(result_or_error.ok());
      IM_CHECK_GT(panel->GetCounters().render_list, initial_list_count);
    };
  }

  // Test 2: Attach with valid ID
  {
    ImGuiTest* t = IM_REGISTER_TEST(engine, "sprite_panel", "attach_valid_id");
    t->TestFunc = [](ImGuiTestContext* ctx) {
      MockApi mock_api;
      EXPECT_CALL(mock_api, GetAllSprites()).WillRepeatedly(Return(std::vector<Sprite>{}));

      Gui gui;
      auto panel_or_error = SpritePanel::Create(&mock_api, &gui);
      IM_CHECK(panel_or_error.ok());
      auto panel = std::move(panel_or_error.value());

      Sprite valid_sprite;
      valid_sprite.id = "valid_id";
      valid_sprite.name = "Valid Sprite";

      EXPECT_CALL(mock_api, GetSprite("valid_id")).WillOnce(Return(&valid_sprite));

      absl::Status status = panel->Attach("valid_id");
      IM_CHECK(status.ok());
    };
  }

  // Test 3: Attach and Render
  {
    ImGuiTest* t = IM_REGISTER_TEST(engine, "sprite_panel", "attach_and_render");
    t->TestFunc = [](ImGuiTestContext* ctx) {
      MockApi mock_api;
      EXPECT_CALL(mock_api, GetAllSprites()).WillRepeatedly(Return(std::vector<Sprite>{}));

      Gui gui;
      auto panel_or_error = SpritePanel::Create(&mock_api, &gui);
      IM_CHECK(panel_or_error.ok());
      auto panel = std::move(panel_or_error.value());

      Sprite valid_sprite;
      valid_sprite.id = "valid_id";
      valid_sprite.name = "Valid Sprite";

      EXPECT_CALL(mock_api, GetSprite("valid_id")).WillOnce(Return(&valid_sprite));

      IM_CHECK(panel->Attach("valid_id").ok());

      // We expect RenderDetails to be called now.
      int initial_details_count = panel->GetCounters().render_details;
      auto result_or_error = panel->Render();
      IM_CHECK(result_or_error.ok());
      IM_CHECK_GT(panel->GetCounters().render_details, initial_details_count);
    };
  }

  // Test 4: Attach -> Render -> Detach
  {
    ImGuiTest* t = IM_REGISTER_TEST(engine, "sprite_panel", "attach_render_detach");
    t->TestFunc = [](ImGuiTestContext* ctx) {
      MockApi mock_api;
      EXPECT_CALL(mock_api, GetAllSprites()).WillRepeatedly(Return(std::vector<Sprite>{}));

      Gui gui;
      auto panel_or_error = SpritePanel::Create(&mock_api, &gui);
      IM_CHECK(panel_or_error.ok());
      auto panel = std::move(panel_or_error.value());

      Sprite valid_sprite;
      valid_sprite.id = "valid_id";
      valid_sprite.name = "Valid Sprite";

      EXPECT_CALL(mock_api, GetSprite("valid_id")).WillOnce(Return(&valid_sprite));

      IM_CHECK(panel->Attach("valid_id").ok());
      IM_CHECK(panel->Render().ok());  // Establish render state

      // Detach
      panel->Detach();

      // Verify state is cleared by checking that Render() now calls RenderList again
      int initial_list_count = panel->GetCounters().render_list;
      int initial_details_count = panel->GetCounters().render_details;

      auto result_or_error = panel->Render();
      IM_CHECK(result_or_error.ok());

      // List count should increase, details count should NOT increase
      IM_CHECK_GT(panel->GetCounters().render_list, initial_list_count);
      IM_CHECK_EQ(panel->GetCounters().render_details, initial_details_count);
    };
  }
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) { return RunTestApp(argc, argv, zebes::RegisterTests); }
