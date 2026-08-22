#include "editor/level_editor/level_editor.h"

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/level.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"
#include "tests/editor/mock_level_panel.h"

namespace zebes {

class LevelEditorTestPeer {
 public:
  static absl::Status RenderNavigator(LevelEditor& editor) { return editor.RenderNavigator(); }

  static absl::Status RenderInspector(LevelEditor& editor) { return editor.RenderInspector(); }

  static absl::Status RenderToolbar(LevelEditor& editor) { return editor.RenderToolbar(); }

  static void SetEditingLevel(LevelEditor& editor, std::optional<Level> level) {
    if (level.has_value()) {
      editor.level_model_.BeginEditingLevel(std::move(*level));
    } else {
      editor.level_model_.CloseActiveLevel();
    }
  }

  static void SetSelection(LevelEditor& editor, SelectionState selection) {
    editor.selection_ = std::move(selection);
  }

  static bool HasEditingLevel(const LevelEditor& editor) {
    return editor.level_model_.has_active_level();
  }

  static bool HasSaveError(const LevelEditor& editor) { return editor.save_error_.has_value(); }

  static LevelPanelModel& GetLevelModel(LevelEditor& editor) { return editor.level_model_; }

  static Level& GetEditingLevel(LevelEditor& editor) { return *editor.level_model_.active_level(); }

  static SelectionState::Type GetSelectionType(const LevelEditor& editor) {
    return editor.selection_.type;
  }

  static int GetSelectedWorldLayerId(const LevelEditor& editor) {
    return editor.selection_.world_layer_id;
  }

  static absl::Status HandleLevelPanelEvent(LevelEditor& editor, LevelPanelEvent event) {
    return editor.HandleLevelPanelEvent(event);
  }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrEq;

// Standalone Create validation tests.
TEST(LevelEditorCreateTest, FailsWithNullApi) {
  NiceMock<MockGui> gui;
  auto result = LevelEditor::Create({.api = nullptr, .gui = &gui});
  EXPECT_FALSE(result.ok());
}

TEST(LevelEditorCreateTest, FailsWithNullGui) {
  NiceMock<MockApi> api;
  auto result = LevelEditor::Create({.api = &api, .gui = nullptr});
  EXPECT_FALSE(result.ok());
}

TEST(SelectionStateTest, EmptyViewportPickPreservesZoneSelection) {
  SelectionState selection{.type = SelectionState::Type::kZone, .zone_id = 7};

  selection.ApplyEntityPick(Entity::kInvalidId);

  EXPECT_EQ(selection.type, SelectionState::Type::kZone);
  EXPECT_EQ(selection.zone_id, 7);
}

TEST(SelectionStateTest, EmptyViewportPickClearsEntitySelection) {
  SelectionState selection{.type = SelectionState::Type::kEntity, .entity_id = 42};

  selection.ApplyEntityPick(Entity::kInvalidId);

  EXPECT_EQ(selection.type, SelectionState::Type::kNone);
  EXPECT_EQ(selection.entity_id, Entity::kInvalidId);
}

TEST(SelectionStateTest, EntityPickReplacesOtherSelection) {
  SelectionState selection{.type = SelectionState::Type::kZone, .zone_id = 7};

  selection.ApplyEntityPick(42);

  EXPECT_EQ(selection.type, SelectionState::Type::kEntity);
  EXPECT_EQ(selection.entity_id, 42);
  EXPECT_EQ(selection.zone_id, -1);
}

TEST(LevelEditorPanelLayoutTest, ReservesBoundedPaletteBelowWorkspace) {
  const LevelEditorPanelLayout layout = CalculateLevelEditorPanelLayout(1000.0f);

  EXPECT_FLOAT_EQ(layout.workspace_height, 742.0f);
  EXPECT_FLOAT_EQ(layout.palette_height, 250.0f);
}

TEST(LevelEditorPanelLayoutTest, KeepsBothPanelsReachableWhenHeightIsConstrained) {
  const LevelEditorPanelLayout layout = CalculateLevelEditorPanelLayout(300.0f);

  EXPECT_FLOAT_EQ(layout.workspace_height, 175.2f);
  EXPECT_FLOAT_EQ(layout.palette_height, 116.8f);
}

TEST(LevelEditorPanelLayoutTest, ClampsNegativeAvailableHeight) {
  const LevelEditorPanelLayout layout = CalculateLevelEditorPanelLayout(-10.0f);

  EXPECT_FLOAT_EQ(layout.workspace_height, 0.0f);
  EXPECT_FLOAT_EQ(layout.palette_height, 0.0f);
}

TEST(LevelEditorPanelLayoutTest, HonorsResizablePaletteWithoutHidingWorkspace) {
  const LevelEditorPanelLayout layout =
      CalculateLevelEditorPanelLayout(1000.0f, /*show_palette=*/true, 400.0f);

  EXPECT_FLOAT_EQ(layout.workspace_height, 592.0f);
  EXPECT_FLOAT_EQ(layout.palette_height, 400.0f);
}

TEST(LevelEditorPanelLayoutTest, HiddenPaletteGivesWorkspaceTheFullHeight) {
  const LevelEditorPanelLayout layout =
      CalculateLevelEditorPanelLayout(1000.0f, /*show_palette=*/false, 400.0f);

  EXPECT_FLOAT_EQ(layout.workspace_height, 1000.0f);
  EXPECT_FLOAT_EQ(layout.palette_height, 0.0f);
}

class LevelEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up API defaults needed for concrete sub-panel construction.
    // ParallaxThemePanel calls GetAllTextures() during Create().
    ON_CALL(*api_, GetAllTextures()).WillByDefault(Return(std::vector<Texture>{}));

    // GUI defaults.
    static ImGuiIO io;
    ON_CALL(gui_, GetIO()).WillByDefault(ReturnRef(io));
    static ImGuiStyle style;
    ON_CALL(gui_, GetStyle()).WillByDefault(ReturnRef(style));
    ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(800, 600)));

    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(
            [&](ImGuiCol idx, const ImVec4& col) { return ScopedStyleColor(&gui_, {}, {}); });
    ON_CALL(gui_, CreateScopedId(::testing::A<const char*>()))
        .WillByDefault(Invoke([this](const char* id) { return ScopedId(&gui_, id); }));
    ON_CALL(gui_, CreateScopedId(::testing::A<int>())).WillByDefault(Invoke([this](int id) {
      return ScopedId(&gui_, id);
    }));
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(
            Invoke([this](const char* label, const char* preview, ImGuiComboFlags flags) {
              return ScopedCombo(&gui_, label, preview, flags);
            }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));

    // All buttons return false by default; individual tests override as needed.
    EXPECT_CALL(gui_, Button(_, _)).WillRepeatedly(Return(false));
    EXPECT_CALL(gui_, Selectable(_, An<bool>(), _, _)).WillRepeatedly(Return(false));

    // Retain a raw pointer to the mock level panel for EXPECT_CALL after the
    // unique_ptr is moved into LevelEditor::Options.
    auto mock_panel = std::make_unique<NiceMock<MockLevelPanel>>();
    mock_level_panel_ = mock_panel.get();

    ON_CALL(*mock_level_panel_, RenderList(_)).WillByDefault(Return(LevelPanelEvent{}));
    ON_CALL(*mock_level_panel_, RenderDetails(_)).WillByDefault(Return(LevelPanelEvent{}));
    ON_CALL(*mock_level_panel_, RenderToolbar(_, _)).WillByDefault(Return(LevelPanelEvent{}));

    auto editor_or = LevelEditor::Create({
        .api = api_.get(),
        .gui = &gui_,
        .level_panel = std::move(mock_panel),
    });
    ASSERT_OK(editor_or);
    editor_ = *std::move(editor_or);
  }

  std::unique_ptr<MockApi> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<LevelEditor> editor_;
  MockLevelPanel* mock_level_panel_ = nullptr;
};

// --- RenderNavigator: no level loaded ---

TEST_F(LevelEditorTest, RenderNavigatorNoLevelDelegatesToLevelPanel) {
  EXPECT_CALL(*mock_level_panel_, RenderList(_)).WillOnce(Return(LevelPanelEvent{}));

  ASSERT_OK(LevelEditorTestPeer::RenderNavigator(*editor_));
}

TEST_F(LevelEditorTest, RenderNavigatorExposesLevelSettingsWithoutCollapsibleSceneRoot) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "cave", .name = "Cave"});
  EXPECT_CALL(gui_, CollapsingHeader(_, _)).Times(0);
  EXPECT_CALL(gui_, Selectable(StrEq("Level Settings##level_settings"), false, _, _))
      .WillOnce(Return(true));

  ASSERT_OK(LevelEditorTestPeer::RenderNavigator(*editor_));

  EXPECT_EQ(LevelEditorTestPeer::GetSelectionType(*editor_), SelectionState::Type::kLevel);
}

TEST_F(LevelEditorTest, RenderNavigatorSelectsAndActivatesAnAlwaysVisibleWorldLayer) {
  Level level{.id = "cave", .name = "Cave"};
  level.layers.push_back(WorldLayer{.id = 7, .name = "Front Decor"});
  LevelEditorTestPeer::SetEditingLevel(*editor_, std::move(level));
  EXPECT_CALL(gui_, Selectable(StrEq("Front Decor##world_layer"), false, _, _))
      .WillOnce(Return(true));

  ASSERT_OK(LevelEditorTestPeer::RenderNavigator(*editor_));

  EXPECT_EQ(LevelEditorTestPeer::GetSelectionType(*editor_), SelectionState::Type::kWorldLayer);
  EXPECT_EQ(LevelEditorTestPeer::GetSelectedWorldLayerId(*editor_), 7);
}

// --- Toolbar: level persistence ---

TEST_F(LevelEditorTest, RenderToolbarSaveLevelSuccessCallsUpdateAndRefreshes) {
  LevelEditorTestPeer::SetEditingLevel(
      *editor_, Level{.id = "a-id", .name = "Alpha", .width = 32, .height = 32});

  EXPECT_CALL(*mock_level_panel_, RenderToolbar(_, _))
      .WillOnce(Return(LevelPanelEvent{.action = LevelPanelAction::kSave}));
  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*api_, GetAllLevels()).WillOnce(Return(std::vector<Level>{}));

  ASSERT_OK(LevelEditorTestPeer::RenderToolbar(*editor_));
  EXPECT_FALSE(LevelEditorTestPeer::HasSaveError(*editor_));
}

TEST_F(LevelEditorTest, RenderToolbarSaveLevelFailureDoesNotCallRefresh) {
  LevelEditorTestPeer::SetEditingLevel(
      *editor_, Level{.id = "a-id", .name = "Alpha", .width = 32, .height = 32});

  EXPECT_CALL(*mock_level_panel_, RenderToolbar(_, _))
      .WillOnce(Return(LevelPanelEvent{.action = LevelPanelAction::kSave}));
  EXPECT_CALL(*api_, UpdateLevel(_)).WillOnce(Return(absl::InternalError("disk full")));
  EXPECT_CALL(*api_, GetAllLevels()).Times(0);

  ASSERT_OK(LevelEditorTestPeer::RenderToolbar(*editor_));
  EXPECT_TRUE(LevelEditorTestPeer::HasSaveError(*editor_));
}

// --- Toolbar: close level ---

TEST_F(LevelEditorTest, RenderToolbarCloseLevelDelegatesListOnNextNavigatorRender) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  EXPECT_CALL(*mock_level_panel_, RenderToolbar(_, _))
      .WillOnce(Return(LevelPanelEvent{.action = LevelPanelAction::kClose}));
  ASSERT_OK(LevelEditorTestPeer::RenderToolbar(*editor_));

  EXPECT_CALL(*mock_level_panel_, RenderList(_)).WillOnce(Return(LevelPanelEvent{}));
  ASSERT_OK(LevelEditorTestPeer::RenderNavigator(*editor_));
}

TEST_F(LevelEditorTest, ReviewIssuesSelectsLevelSettings) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id", .name = "Alpha"});

  ASSERT_OK(LevelEditorTestPeer::HandleLevelPanelEvent(
      *editor_, LevelPanelEvent{.action = LevelPanelAction::kReviewIssues}));

  EXPECT_EQ(LevelEditorTestPeer::GetSelectionType(*editor_), SelectionState::Type::kLevel);
}

// --- RenderInspector ---

TEST_F(LevelEditorTest, RenderInspectorNoLevelDoesNotDelegateToLevelPanel) {
  EXPECT_CALL(*mock_level_panel_, RenderDetails(_)).Times(0);

  ASSERT_OK(LevelEditorTestPeer::RenderInspector(*editor_));
}

TEST_F(LevelEditorTest, RenderInspectorLevelSelectionDelegatesToLevelPanel) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id"});
  SelectionState selection;
  selection.type = SelectionState::Type::kLevel;
  LevelEditorTestPeer::SetSelection(*editor_, selection);

  EXPECT_CALL(*mock_level_panel_, RenderDetails(_)).WillOnce(Return(LevelPanelEvent{}));

  ASSERT_OK(LevelEditorTestPeer::RenderInspector(*editor_));
}

TEST_F(LevelEditorTest, RenderInspectorNoSelectionDoesNotDelegateToLevelPanel) {
  LevelEditorTestPeer::SetEditingLevel(*editor_, Level{.id = "a-id"});
  // selection_.type == kNone by default.

  EXPECT_CALL(*mock_level_panel_, RenderDetails(_)).Times(0);

  ASSERT_OK(LevelEditorTestPeer::RenderInspector(*editor_));
}

TEST_F(LevelEditorTest, RenderInspectorResnapsSelectedBlueprintEntityToNearestAnchor) {
  Blueprint blueprint{
      .id = "crystal-blueprint",
      .states = {{.name = "Default", .placement_mode = BlueprintPlacementMode::kGrounded}},
  };
  Level level{
      .id = "cave",
      .tile_render_width = 32,
      .tile_render_height = 32,
  };
  level.layers.front().entities.emplace(7, Entity{.id = 7,
                                                  .transform = {.position = {176, 440}},
                                                  .blueprint_id = blueprint.id,
                                                  .blueprint_state_index = 0});
  LevelEditorTestPeer::SetEditingLevel(*editor_, std::move(level));
  LevelEditorTestPeer::SetSelection(
      *editor_, SelectionState{.type = SelectionState::Type::kEntity, .entity_id = 7});
  EXPECT_CALL(*api_, GetBlueprint(StrEq("crystal-blueprint"))).WillOnce(Return(&blueprint));
  EXPECT_CALL(gui_, Button(StrEq("Resnap to Grid"), _)).WillOnce(Return(true));

  ASSERT_OK(LevelEditorTestPeer::RenderInspector(*editor_));

  const Entity& entity =
      LevelEditorTestPeer::GetEditingLevel(*editor_).layers.front().entities.at(7);
  EXPECT_EQ(entity.transform.position, (Vec{176, 448}));
}

TEST_F(LevelEditorTest, RenderInspectorRejectsInvalidBlueprintStateDuringResnap) {
  Blueprint blueprint{
      .id = "crystal-blueprint",
      .states = {{.name = "Default", .placement_mode = BlueprintPlacementMode::kGrounded}},
  };
  Level level{.id = "cave"};
  level.layers.front().entities.emplace(7, Entity{.id = 7,
                                                  .transform = {.position = {176, 440}},
                                                  .blueprint_id = blueprint.id,
                                                  .blueprint_state_index = 3});
  LevelEditorTestPeer::SetEditingLevel(*editor_, std::move(level));
  LevelEditorTestPeer::SetSelection(
      *editor_, SelectionState{.type = SelectionState::Type::kEntity, .entity_id = 7});
  EXPECT_CALL(*api_, GetBlueprint(StrEq("crystal-blueprint"))).WillOnce(Return(&blueprint));
  EXPECT_CALL(gui_, Button(StrEq("Resnap to Grid"), _)).WillOnce(Return(true));

  EXPECT_EQ(LevelEditorTestPeer::RenderInspector(*editor_).code(),
            absl::StatusCode::kInvalidArgument);
  const Entity& entity =
      LevelEditorTestPeer::GetEditingLevel(*editor_).layers.front().entities.at(7);
  EXPECT_EQ(entity.transform.position, (Vec{176, 440}));
}

TEST_F(LevelEditorTest, CreateEventPersistsDraftAndSelectsLevel) {
  LevelPanelModel& model = LevelEditorTestPeer::GetLevelModel(*editor_);
  model.BeginNewLevel();
  model.active_level()->width = 32;
  model.active_level()->height = 32;
  EXPECT_CALL(*api_, CreateLevel(_)).WillOnce(Return(std::string("new-id")));
  EXPECT_CALL(*api_, GetAllLevels()).WillOnce(Return(std::vector<Level>{}));

  ASSERT_OK(LevelEditorTestPeer::HandleLevelPanelEvent(
      *editor_, LevelPanelEvent{.action = LevelPanelAction::kCreate}));

  ASSERT_NE(model.active_level(), nullptr);
  EXPECT_EQ(model.active_level()->id, "new-id");
  EXPECT_EQ(LevelEditorTestPeer::GetSelectionType(*editor_), SelectionState::Type::kLevel);
}

TEST_F(LevelEditorTest, FailedDeletePreservesModelSelection) {
  LevelPanelModel& model = LevelEditorTestPeer::GetLevelModel(*editor_);
  model.SetLevels({{.id = "cave", .name = "Cave"}});
  ASSERT_OK(model.SelectLevel("cave"));
  EXPECT_CALL(*api_, DeleteLevel(StrEq("cave"))).WillOnce(Return(absl::InternalError("disk full")));
  EXPECT_CALL(*api_, GetAllLevels()).Times(0);

  EXPECT_FALSE(LevelEditorTestPeer::HandleLevelPanelEvent(
                   *editor_, LevelPanelEvent{.action = LevelPanelAction::kDelete})
                   .ok());

  EXPECT_EQ(model.selected_level_id(), "cave");
}

}  // namespace
}  // namespace zebes
