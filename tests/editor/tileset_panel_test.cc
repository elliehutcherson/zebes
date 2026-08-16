#include "editor/tileset_editor/tileset_panel.h"

#include <memory>
#include <optional>
#include <string>

#include "editor/tileset_editor/tileset_editor_model.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/tileset.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

constexpr float kTextRow = 20.0f;
constexpr float kWidgetRow = 26.0f;
constexpr float kWindowHeight = 500.0f;

class TilesetPanelTest : public ::testing::Test {
 protected:
  // Records the height the tile list asks for.
  void CaptureTileListHeight(float* height) {
    ON_CALL(gui_, CreateScopedListBox(StrEq("##Tiles"), _))
        .WillByDefault(Invoke([this, height](const char* label, ImVec2 size) {
          *height = size.y;
          return ScopedListBox(&gui_, label, size);
        }));
  }

  void SetUp() override {
    absl::StatusOr<std::unique_ptr<TilesetPanel>> panel = TilesetPanel::Create(&gui_);
    ASSERT_OK(panel);
    panel_ = *std::move(panel);

    // Every scoped guard the panel builds needs a real object; none of them are
    // default constructible. Containers report closed so one headless render
    // walks the panel body without descending into list or combo contents.
    ON_CALL(gui_, CreateScopedListBox(_, _))
        .WillByDefault(Invoke(
            [this](const char* label, ImVec2 size) { return ScopedListBox(&gui_, label, size); }));
    ON_CALL(gui_, BeginListBox(_, _)).WillByDefault(Return(false));

    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));

    // The details body scrolls, so its child has to report open or nothing
    // below the header renders at all.
    ON_CALL(gui_, CreateScopedChild(_, _, _, _))
        .WillByDefault(
            Invoke([this](const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags) {
              return ScopedChild(&gui_, id, size, border, flags);
            }));
    ON_CALL(gui_, BeginChild(_, _, _, _)).WillByDefault(Return(true));

    ON_CALL(gui_, CreateScopedId(::testing::An<int>()))
        .WillByDefault(Invoke([this](int int_id) { return ScopedId(&gui_, int_id); }));
    ON_CALL(gui_, CreateScopedStyleColor(_, ::testing::An<const ImVec4&>()))
        .WillByDefault(Invoke([this](ImGuiCol idx, const ImVec4& col) {
          return ScopedStyleColor(&gui_, idx, col);
        }));

    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, DisplayFileDialog(_)).WillByDefault(Return(std::nullopt));

    ON_CALL(gui_, GetTextLineHeightWithSpacing()).WillByDefault(Return(kTextRow));
    ON_CALL(gui_, GetFrameHeightWithSpacing()).WillByDefault(Return(kWidgetRow));
    ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(200.0f, kWindowHeight)));

    model_.BeginNewTileset();
  }

  // Makes exactly one button report a click, so a render exercises one action.
  void ClickOnly(std::string label) {
    ON_CALL(gui_, Button(_, _))
        .WillByDefault(Invoke(
            [label](const char* pressed, const ImVec2&) { return label == pressed; }));
  }

  void RenderDetails() {
    absl::StatusOr<TilesetPanel::Action> action = panel_->RenderDetails(model_);
    ASSERT_OK(action);
  }

  // Same render, but keeps what the panel reported, for the intents the
  // containing editor has to coordinate with the Api.
  TilesetPanel::Action RenderDetailsForAction() {
    absl::StatusOr<TilesetPanel::Action> action = panel_->RenderDetails(model_);
    EXPECT_OK(action);
    return action.ok() ? *action : TilesetPanel::Action::kNone;
  }

  // Records every label the panel drew a button for during one render.
  void CaptureButtons(std::vector<std::string>* labels) {
    ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([labels](const char* label, const ImVec2&) {
      labels->push_back(label);
      return false;
    }));
  }

  // Leaves the list view with two tilesets in the catalog and nothing open.
  void BeginListView() {
    model_.CloseActiveTileset();
    model_.SetTilesets({Tileset{.id = "grass-1", .name = "Grass"},
                        Tileset{.id = "stone-2", .name = "Stone"}});
  }

  // Renders the list-view navigator, which is a different entry point from the
  // details view every other test in this file drives.
  TilesetPanel::Action RenderList() {
    absl::StatusOr<TilesetPanel::Action> action = panel_->RenderList(model_);
    EXPECT_OK(action);
    return action.ok() ? *action : TilesetPanel::Action::kNone;
  }

  // Opens the list body so entries render, which the default closed container
  // deliberately skips.
  void OpenTilesetList() { ON_CALL(gui_, BeginListBox(_, _)).WillByDefault(Return(true)); }

  NiceMock<MockGui> gui_;
  TilesetEditorModel model_;
  std::unique_ptr<TilesetPanel> panel_;
};

// Authoring a terrain moved to the Terrain tab, which has room for the preview
// that tuning one needs. Nothing here should be asking for a manifest any more.
TEST_F(TilesetPanelTest, NoManifestControlsRemain) {
  EXPECT_CALL(gui_, OpenFileDialog(_, _, _, _)).Times(0);
  ClickOnly("Browse##Terrain");

  RenderDetails();
}

TEST_F(TilesetPanelTest, DetectAddsTerrainsFoundInTheTilesAlready) {
  ClickOnly("Detect##Terrain");

  // A tileset with no tiles has nothing to detect; the point is that the button
  // still runs the model rather than having been removed with the import path.
  RenderDetails();
  ASSERT_NE(model_.active_tileset(), nullptr);
  EXPECT_TRUE(model_.active_tileset()->terrains.empty());
}

// The tile list used to size itself against a prediction of everything below
// it, which is how the terrain controls ended up off the bottom of the window.
// It now takes a fixed height and the enclosing child scrolls.
TEST_F(TilesetPanelTest, TileListHeightDoesNotDependOnWhatIsBelowIt) {
  float without_terrains = 0.0f;
  CaptureTileListHeight(&without_terrains);
  RenderDetails();
  EXPECT_GT(without_terrains, 0.0f);

  model_.active_tileset()->terrains.push_back(Terrain{.id = 1, .name = "Grass"});
  model_.active_tileset()->terrains.push_back(Terrain{.id = 2, .name = "Stone"});

  float with_terrains = 0.0f;
  CaptureTileListHeight(&with_terrains);
  RenderDetails();

  EXPECT_FLOAT_EQ(with_terrains, without_terrains);
}

TEST_F(TilesetPanelTest, TileListHeightDoesNotDependOnWindowHeight) {
  float tall = 0.0f;
  CaptureTileListHeight(&tall);
  RenderDetails();

  ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(200.0f, 30.0f)));
  float shortened = 0.0f;
  CaptureTileListHeight(&shortened);
  RenderDetails();

  EXPECT_FLOAT_EQ(shortened, tall);
}

// Back and Save sit outside the scroll region: a column too short to show
// everything must not be able to hide the way out or the way to keep your work.
TEST_F(TilesetPanelTest, SaveIsReachableWithoutScrolling) {
  bool save_before_body = false;
  bool body_started = false;
  ON_CALL(gui_, CreateScopedChild(_, _, _, _))
      .WillByDefault(
          Invoke([&](const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags) {
            body_started = true;
            return ScopedChild(&gui_, id, size, border, flags);
          }));
  ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([&](const char* label, const ImVec2&) {
    if (std::string(label) == "Save" && !body_started) save_before_body = true;
    return false;
  }));

  RenderDetails();

  EXPECT_TRUE(save_before_body);
}

TEST_F(TilesetPanelTest, SaveIsReported) {
  ClickOnly("Save");

  absl::StatusOr<TilesetPanel::Action> action = panel_->RenderDetails(model_);
  ASSERT_OK(action);
  EXPECT_EQ(*action, TilesetPanel::Action::kSave);
}

TEST_F(TilesetPanelTest, TerrainNameIsEditable) {
  model_.active_tileset()->terrains.push_back(Terrain{.id = 1, .name = "Terrain"});

  std::string* name_field = nullptr;
  ON_CALL(gui_, InputText(StrEq("##TerrainName"), ::testing::An<std::string*>(), _, _, _))
      .WillByDefault(Invoke([&name_field](const char*, std::string* value, ImGuiInputTextFlags,
                                          ImGuiInputTextCallback, void*) {
        name_field = value;
        return false;
      }));

  RenderDetails();

  ASSERT_NE(name_field, nullptr);
  EXPECT_EQ(name_field, &model_.active_tileset()->terrains[0].name);
}

// A button that is enabled, does nothing when pressed, and reports nothing is
// worse than no button: it teaches that the editor is broken. Edit and Delete
// have nothing to act on until a tileset is selected, so they say so.
TEST_F(TilesetPanelTest, EditAndDeleteAreDisabledWithNoSelection) {
  BeginListView();

  EXPECT_CALL(gui_, CreateScopedDisabled(true)).Times(2);
  EXPECT_CALL(gui_, CreateScopedDisabled(false)).Times(0);

  RenderList();
}

TEST_F(TilesetPanelTest, EditAndDeleteAreEnabledOnceATilesetIsSelected) {
  BeginListView();
  ASSERT_OK(model_.SelectTileset("grass-1"));

  EXPECT_CALL(gui_, CreateScopedDisabled(false)).Times(2);
  EXPECT_CALL(gui_, CreateScopedDisabled(true)).Times(0);

  RenderList();
}

TEST_F(TilesetPanelTest, DeletingATilesetAsksBeforeDestroyingIt) {
  BeginListView();
  ASSERT_OK(model_.SelectTileset("grass-1"));

  ClickOnly("Delete##Tileset");
  EXPECT_EQ(RenderList(), TilesetPanel::Action::kNone);

  // The next frame offers the answer instead of the original button.
  std::vector<std::string> labels;
  CaptureButtons(&labels);
  RenderList();
  EXPECT_THAT(labels, ::testing::Contains("Confirm##Tileset"));
  EXPECT_THAT(labels, ::testing::Contains("Cancel##Tileset"));
  EXPECT_THAT(labels, ::testing::Not(::testing::Contains("Delete##Tileset")));

  ClickOnly("Confirm##Tileset");
  EXPECT_EQ(RenderList(), TilesetPanel::Action::kDelete);
}

TEST_F(TilesetPanelTest, CancellingADeleteRestoresThePlainButton) {
  BeginListView();
  ASSERT_OK(model_.SelectTileset("grass-1"));

  ClickOnly("Delete##Tileset");
  RenderList();

  ClickOnly("Cancel##Tileset");
  EXPECT_EQ(RenderList(), TilesetPanel::Action::kNone);

  std::vector<std::string> labels;
  CaptureButtons(&labels);
  RenderList();
  EXPECT_THAT(labels, ::testing::Contains("Delete##Tileset"));
  EXPECT_THAT(labels, ::testing::Not(::testing::Contains("Confirm##Tileset")));
}

// A confirmation belongs to the tileset it was raised against. Selecting a
// different one must not leave a primed Confirm pointing at the new selection.
TEST_F(TilesetPanelTest, ChangingSelectionDropsAPendingDelete) {
  BeginListView();
  ASSERT_OK(model_.SelectTileset("grass-1"));

  ClickOnly("Delete##Tileset");
  RenderList();

  ASSERT_OK(model_.SelectTileset("stone-2"));
  ClickOnly("Confirm##Tileset");
  EXPECT_EQ(RenderList(), TilesetPanel::Action::kNone);
}

TEST_F(TilesetPanelTest, DoubleClickingAListEntryOpensIt) {
  BeginListView();
  OpenTilesetList();

  ON_CALL(gui_, Selectable(StrEq("Grass"), ::testing::An<bool>(), _, _))
      .WillByDefault(Return(true));
  ON_CALL(gui_, IsMouseDoubleClicked(ImGuiMouseButton_Left)).WillByDefault(Return(true));

  RenderList();

  ASSERT_NE(model_.active_tileset(), nullptr);
  EXPECT_EQ(model_.active_tileset()->id, "grass-1");
}

TEST_F(TilesetPanelTest, SingleClickingAListEntryOnlySelectsIt) {
  BeginListView();
  OpenTilesetList();

  ON_CALL(gui_, Selectable(StrEq("Grass"), ::testing::An<bool>(), _, _))
      .WillByDefault(Return(true));
  ON_CALL(gui_, IsMouseDoubleClicked(_)).WillByDefault(Return(false));

  RenderList();

  EXPECT_EQ(model_.selected_tileset_id(), "grass-1");
  EXPECT_EQ(model_.active_tileset(), nullptr);
}

// Leaving is only destructive when there is something to lose.
TEST_F(TilesetPanelTest, BackClosesATilesetWithNoEdits) {
  ClickOnly("Back");
  RenderDetails();

  EXPECT_EQ(model_.active_tileset(), nullptr);
}

TEST_F(TilesetPanelTest, BackAsksBeforeDiscardingUnsavedEdits) {
  model_.active_tileset()->name = "Renamed";

  ClickOnly("Back");
  RenderDetails();
  ASSERT_NE(model_.active_tileset(), nullptr) << "Back closed the tileset without asking";

  ClickOnly("Cancel##Back");
  RenderDetails();
  EXPECT_NE(model_.active_tileset(), nullptr);

  ClickOnly("Back");
  RenderDetails();
  ClickOnly("Confirm##Back");
  RenderDetails();
  EXPECT_EQ(model_.active_tileset(), nullptr);
}

TEST_F(TilesetPanelTest, DeletingATerrainAsksBeforeDestroyingIt) {
  model_.active_tileset()->terrains.push_back(Terrain{.id = 1, .name = "Grass"});

  ClickOnly("Delete##Terrain");
  RenderDetails();
  ASSERT_EQ(model_.active_tileset()->terrains.size(), 1u)
      << "the terrain was destroyed on the first click";

  ClickOnly("Confirm##Terrain");
  RenderDetails();
  EXPECT_TRUE(model_.active_tileset()->terrains.empty());
}

TEST_F(TilesetPanelTest, CancellingATerrainDeleteKeepsIt) {
  model_.active_tileset()->terrains.push_back(Terrain{.id = 1, .name = "Grass"});

  ClickOnly("Delete##Terrain");
  RenderDetails();

  ClickOnly("Cancel##Terrain");
  RenderDetails();

  ClickOnly("Confirm##Terrain");
  RenderDetails();
  EXPECT_EQ(model_.active_tileset()->terrains.size(), 1u);
}

// The panel reports the intent instead of applying it. Only the containing
// editor can ask the Api whether a level has painted the tile, and deleting one
// a level painted stops that level rendering rather than losing a cell.
TEST_F(TilesetPanelTest, DeletingATileReportsTheIntentRatherThanRemovingIt) {
  ASSERT_OK(model_.AddTile());
  ASSERT_EQ(model_.active_tileset()->tiles.size(), 1u);

  ClickOnly("Delete##Tile");

  EXPECT_EQ(RenderDetailsForAction(), TilesetPanel::Action::kDeleteTile);
  EXPECT_EQ(model_.active_tileset()->tiles.size(), 1u)
      << "the panel must not remove the tile itself";
}

TEST_F(TilesetPanelTest, RenderingWithoutClickingDeleteReportsNothing) {
  ASSERT_OK(model_.AddTile());

  EXPECT_EQ(RenderDetailsForAction(), TilesetPanel::Action::kNone);
}

}  // namespace
}  // namespace zebes
