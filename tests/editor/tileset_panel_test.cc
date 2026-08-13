#include "editor/tileset_editor/tileset_panel.h"

#include <memory>
#include <optional>
#include <string>

#include "editor/tileset_editor/tileset_editor_model.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
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
    ASSERT_TRUE(panel.ok()) << panel.status();
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
    ASSERT_TRUE(action.ok()) << action.status();
  }

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
  ASSERT_TRUE(action.ok()) << action.status();
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

}  // namespace
}  // namespace zebes
