#include "editor/tileset_editor/tileset_panel.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "editor/tileset_editor/tileset_editor_model.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/tileset.h"
#include "terrain/blob47_compose.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

// The manifest a real compose_blob47 run emits, at its smallest valid size.
std::string MakeManifest() {
  QuadrantSheet sheet;
  sheet.quadrant_size = 8;
  sheet.variant_count = 1;
  sheet.image.width = sheet.quadrant_size * kQuadrantStateCount;
  sheet.image.height = sheet.quadrant_size * kQuadrantCount;
  sheet.image.pixels.assign(static_cast<size_t>(sheet.image.width) * sheet.image.height * 4, 255);

  absl::StatusOr<Blob47Atlas> atlas = ComposeBlob47(sheet);
  EXPECT_TRUE(atlas.ok()) << atlas.status();
  return WriteBlob47Manifest(*atlas);
}

constexpr float kTextRow = 20.0f;
constexpr float kWidgetRow = 26.0f;
constexpr float kWindowHeight = 500.0f;

class TilesetPanelTest : public ::testing::Test {
 protected:
  // Records the height the tile list asks for, which is what leaves room for
  // the terrain section below it.
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

    ON_CALL(gui_, CreateScopedId(::testing::An<int>()))
        .WillByDefault(Invoke([this](int int_id) { return ScopedId(&gui_, int_id); }));
    ON_CALL(gui_, CreateScopedStyleColor(_, ::testing::An<const ImVec4&>()))
        .WillByDefault(Invoke([this](ImGuiCol idx, const ImVec4& col) {
          return ScopedStyleColor(&gui_, idx, col);
        }));

    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, DisplayFileDialog(_)).WillByDefault(Return(std::nullopt));

    // Layout metrics stand in for a real ImGui frame so height reservation is
    // checkable arithmetic rather than a guess about font size.
    ON_CALL(gui_, GetTextLineHeightWithSpacing()).WillByDefault(Return(kTextRow));
    ON_CALL(gui_, GetFrameHeightWithSpacing()).WillByDefault(Return(kWidgetRow));
    ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(200.0f, kWindowHeight)));

    model_.BeginNewTileset();
  }

  // Makes exactly one button report a click, so a render exercises one action.
  void ClickOnly(std::string label) {
    ON_CALL(gui_, Button(_, _))
        .WillByDefault(Invoke([label](const char* pressed, const ImVec2&) {
          return label == pressed;
        }));
  }

  void RenderDetails() {
    absl::StatusOr<TilesetPanel::Action> action = panel_->RenderDetails(model_);
    ASSERT_TRUE(action.ok()) << action.status();
  }

  NiceMock<MockGui> gui_;
  TilesetEditorModel model_;
  std::unique_ptr<TilesetPanel> panel_;
};

TEST_F(TilesetPanelTest, BrowseOpensAJsonFileDialog) {
  ClickOnly("Browse##Terrain");
  EXPECT_CALL(gui_, OpenFileDialog(StrEq("TerrainManifestDlg"), _, StrEq(".json"), _));

  RenderDetails();
}

TEST_F(TilesetPanelTest, NoDialogOpensWithoutBrowse) {
  EXPECT_CALL(gui_, OpenFileDialog(_, _, _, _)).Times(0);

  RenderDetails();
}

TEST_F(TilesetPanelTest, ChosenPathPopulatesTheManifestField) {
  ON_CALL(gui_, DisplayFileDialog(StrEq("TerrainManifestDlg")))
      .WillByDefault(Return(std::optional<std::string>("/manifests/grass.json")));
  RenderDetails();

  // The dialog reports its result once; the field must hold it afterwards.
  ON_CALL(gui_, DisplayFileDialog(_)).WillByDefault(Return(std::nullopt));
  std::string field;
  ON_CALL(gui_, InputText(StrEq("Manifest##Terrain"), ::testing::An<std::string*>(), _, _, _))
      .WillByDefault(Invoke([&field](const char*, std::string* value, ImGuiInputTextFlags,
                                     ImGuiInputTextCallback, void*) {
        field = *value;
        return false;
      }));

  RenderDetails();

  EXPECT_EQ(field, "/manifests/grass.json");
}

TEST_F(TilesetPanelTest, ImportReadsTheManifestTheDialogChose) {
  const std::string path = std::string(::testing::TempDir()) + "/tileset_panel_manifest.json";
  std::ofstream file(path);
  ASSERT_TRUE(file.is_open());
  file << MakeManifest();
  file.close();

  ON_CALL(gui_, DisplayFileDialog(_))
      .WillByDefault(Return(std::optional<std::string>(path)));
  RenderDetails();

  ClickOnly("Import##Terrain");
  RenderDetails();

  ASSERT_NE(model_.active_tileset(), nullptr);
  ASSERT_EQ(model_.active_tileset()->terrains.size(), 1u);
  EXPECT_EQ(model_.active_tileset()->terrains[0].rules.size(), kBlob47TileCount);
  std::remove(path.c_str());
}

TEST_F(TilesetPanelTest, ImportingAMissingFileAddsNoTerrain) {
  ON_CALL(gui_, DisplayFileDialog(_))
      .WillByDefault(Return(std::optional<std::string>("/nonexistent/manifest.json")));
  RenderDetails();

  ClickOnly("Import##Terrain");
  RenderDetails();

  ASSERT_NE(model_.active_tileset(), nullptr);
  EXPECT_TRUE(model_.active_tileset()->terrains.empty());
}

// Import used to sit below the window bottom because the tile list claimed
// every remaining pixel.
TEST_F(TilesetPanelTest, TileListLeavesRoomForTheTerrainSection) {
  float list_height = 0.0f;
  CaptureTileListHeight(&list_height);

  RenderDetails();

  // Heading and status text, the manifest and import rows, and the empty hint.
  const float reserved = 2.0f * kTextRow + 2.0f * kWidgetRow + kTextRow;
  EXPECT_FLOAT_EQ(list_height, kWindowHeight - reserved);
}

TEST_F(TilesetPanelTest, EachTerrainWidensTheReservation) {
  float without_terrains = 0.0f;
  CaptureTileListHeight(&without_terrains);
  RenderDetails();

  model_.active_tileset()->terrains.push_back(Terrain{.id = 1, .name = "Grass"});
  model_.active_tileset()->terrains.push_back(Terrain{.id = 2, .name = "Stone"});

  float with_terrains = 0.0f;
  CaptureTileListHeight(&with_terrains);
  RenderDetails();

  // Two terrain rows replace the one-line empty hint.
  EXPECT_FLOAT_EQ(with_terrains, without_terrains + kTextRow - 2.0f * kWidgetRow);
}

// Below a minimum the panel scrolls instead of shrinking the list away.
TEST_F(TilesetPanelTest, ShortWindowKeepsAMinimumTileList) {
  ON_CALL(gui_, GetContentRegionAvail()).WillByDefault(Return(ImVec2(200.0f, 30.0f)));
  float list_height = 0.0f;
  CaptureTileListHeight(&list_height);

  RenderDetails();

  EXPECT_FLOAT_EQ(list_height, 4.0f * kTextRow);
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
