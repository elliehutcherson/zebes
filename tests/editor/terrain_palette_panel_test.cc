#include "editor/level_editor/terrain_palette_panel.h"

#include <memory>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/tileset.h"
#include "terrain/terrain_placement.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

class TerrainPalettePanelTestPeer {
 public:
  static void SetSelectedTileset(TerrainPalettePanel& panel, const Tileset* tileset) {
    panel.selected_tileset_ = tileset;
  }
  // Drives the picker's state directly, since choosing from a combo takes more
  // mocked ImGui than the behaviour under test is worth.
  static void SetSelectedShape(TerrainPalettePanel& panel, TileShape shape) {
    panel.selected_shape_ = shape;
  }
};

namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

// Builds a terrain whose solid-mask rule points at tile_id, which is what the
// panel looks up to draw a swatch.
Terrain MakeTerrain(int id, std::string name, int tile_id) {
  Terrain terrain;
  terrain.id = id;
  terrain.name = std::move(name);
  terrain.scheme = TerrainScheme::kBlob47;
  terrain.rules.push_back(
      TerrainRule{.mask = 255, .variants = {TerrainVariant{.tile_id = tile_id}}});
  return terrain;
}

class TerrainPalettePanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<TerrainPalettePanel>> panel_or =
        TerrainPalettePanel::Create({.api = api_.get(), .gui = &gui_});
    ASSERT_TRUE(panel_or.ok()) << panel_or.status();
    panel_ = *std::move(panel_or);

    // Combo closed by default, so Render walks past the tileset selector.
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));

    // Child open by default, so the terrain list body actually runs.
    ON_CALL(gui_, CreateScopedChild(_, _, _, _))
        .WillByDefault(
            Invoke([this](const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags) {
              return ScopedChild(&gui_, id, size, border, flags);
            }));
    ON_CALL(gui_, BeginChild(_, _, _, _)).WillByDefault(Return(true));

    ON_CALL(gui_, GetCursorScreenPos()).WillByDefault(Return(ImVec2(0, 0)));
    // Null draw list keeps the panel headless: it skips the ImGui draw calls
    // and exercises only its own logic.
    ON_CALL(gui_, GetWindowDrawList()).WillByDefault(Return(nullptr));

    ON_CALL(gui_, InvisibleButton(_, _, _)).WillByDefault(Return(false));
    ON_CALL(gui_, IsItemClicked(_)).WillByDefault(Return(false));
    ON_CALL(gui_, IsItemHovered(_)).WillByDefault(Return(false));
    ON_CALL(gui_, SameLine(_, _)).WillByDefault(Return());
    ON_CALL(gui_, CreateScopedId(An<int>())).WillByDefault(Invoke([this](int id) {
      return ScopedId(&gui_, id);
    }));
  }

  // Selects a tileset without driving the combo, matching how the tile palette
  // test avoids the Selectable overload ambiguity.
  void PreselectTileset() {
    ON_CALL(*api_, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{tileset_}));
    ON_CALL(*api_, GetTileset("ts-1")).WillByDefault(Return(&tileset_));
    TerrainPalettePanelTestPeer::SetSelectedTileset(*panel_, &tileset_);
  }

  absl::Status Render() { return panel_->Render(); }

  // How many shape glyphs the picker drew this frame. The grid gives one
  // clickable button per offered shape, so counting them is how a test sees
  // what the picker is offering without a draw list.
  int RenderCountingShapeGlyphs() {
    int glyphs = 0;
    ON_CALL(gui_, InvisibleButton(_, _, _))
        .WillByDefault(Invoke([&glyphs](const char* id, const ImVec2&, ImGuiButtonFlags) {
          if (std::string_view(id) == "##shape") ++glyphs;
          return false;
        }));
    EXPECT_TRUE(Render().ok());
    ON_CALL(gui_, InvisibleButton(_, _, _)).WillByDefault(Return(false));
    return glyphs;
  }

  std::unique_ptr<NiceMock<MockApi>> api_ = std::make_unique<NiceMock<MockApi>>();
  NiceMock<MockGui> gui_;
  std::unique_ptr<TerrainPalettePanel> panel_;

  Tileset tileset_{.id = "ts-1", .name = "Ground", .tile_width = 16, .tile_height = 16};
};

// --- Create validation ---

TEST(TerrainPalettePanelCreateTest, FailsWithNullApi) {
  NiceMock<MockGui> gui;
  EXPECT_FALSE(TerrainPalettePanel::Create({.api = nullptr, .gui = &gui}).ok());
}

TEST(TerrainPalettePanelCreateTest, FailsWithNullGui) {
  NiceMock<MockApi> api;
  EXPECT_FALSE(TerrainPalettePanel::Create({.api = &api, .gui = nullptr}).ok());
}

// --- Empty states ---

TEST_F(TerrainPalettePanelTest, NoTilesetsRendersWithNothingSelected) {
  ON_CALL(*api_, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{}));

  ASSERT_TRUE(Render().ok());
  EXPECT_EQ(panel_->GetSelectedTileset(), nullptr);
  EXPECT_FALSE(panel_->GetSelectedTerrainId().has_value());
}

// A tileset with no terrains is an ordinary authoring state, not an error.
TEST_F(TerrainPalettePanelTest, TilesetWithoutTerrainsRendersAndSelectsNothing) {
  PreselectTileset();

  ASSERT_TRUE(Render().ok());
  EXPECT_EQ(panel_->GetSelectedTileset(), &tileset_);
  EXPECT_FALSE(panel_->GetSelectedTerrainId().has_value());
}

// --- Selection ---

TEST_F(TerrainPalettePanelTest, ClickingATerrainSelectsIt) {
  tileset_.tiles = {Tile{.id = 7, .name = "grass_solid"}};
  tileset_.terrains = {MakeTerrain(3, "Grass", 7)};
  PreselectTileset();

  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));

  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());
  EXPECT_EQ(*panel_->GetSelectedTerrainId(), 3);
}

TEST_F(TerrainPalettePanelTest, ClickingTheSelectedTerrainDeselectsIt) {
  tileset_.tiles = {Tile{.id = 7, .name = "grass_solid"}};
  tileset_.terrains = {MakeTerrain(3, "Grass", 7)};
  PreselectTileset();

  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());

  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  EXPECT_FALSE(panel_->GetSelectedTerrainId().has_value());
}

TEST_F(TerrainPalettePanelTest, ClickingASecondTerrainReplacesTheSelection) {
  tileset_.tiles = {Tile{.id = 7, .name = "grass_solid"}, Tile{.id = 9, .name = "stone_solid"}};
  tileset_.terrains = {MakeTerrain(3, "Grass", 7), MakeTerrain(4, "Stone", 9)};
  PreselectTileset();

  // Second swatch in the list, first frame.
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(false)).WillOnce(Return(true));

  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());
  EXPECT_EQ(*panel_->GetSelectedTerrainId(), 4);
}

TEST_F(TerrainPalettePanelTest, ClearSelectionResetsTheSelectedTerrain) {
  tileset_.tiles = {Tile{.id = 7, .name = "grass_solid"}};
  tileset_.terrains = {MakeTerrain(3, "Grass", 7)};
  PreselectTileset();

  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));
  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());

  panel_->ClearSelection();
  EXPECT_FALSE(panel_->GetSelectedTerrainId().has_value());
}

// --- Texture fallback ---

// An unset texture must still render swatches as placeholders. Failing the
// frame here would make a half-authored tileset unusable in the editor.
TEST_F(TerrainPalettePanelTest, TilesetWithoutATextureStillRendersAndSelects) {
  tileset_.texture_id = "";
  tileset_.tiles = {Tile{.id = 7, .name = "grass_solid"}};
  tileset_.terrains = {MakeTerrain(3, "Grass", 7)};
  PreselectTileset();

  // No texture is resolved at all when the tileset declares none.
  EXPECT_CALL(*api_, GetTextureHandle(_)).Times(0);
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));

  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());
  EXPECT_EQ(*panel_->GetSelectedTerrainId(), 3);
}

// A terrain whose solid-mask tile is missing has no swatch art, but it must
// still be listed and selectable rather than skipped.
TEST_F(TerrainPalettePanelTest, TerrainWithNoSwatchTileIsStillSelectable) {
  tileset_.terrains = {MakeTerrain(3, "Grass", /*tile_id=*/42)};  // no such tile
  PreselectTileset();

  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true));

  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());
  EXPECT_EQ(*panel_->GetSelectedTerrainId(), 3);
}

// --- Shape selection ---------------------------------------------------------

TEST_F(TerrainPalettePanelTest, TheBrushPaintsBlocksUntilSomethingElseIsChosen) {
  EXPECT_EQ(panel_->GetSelectedShape(), TileShape::kFullBlock);
}

TEST_F(TerrainPalettePanelTest, TheShapePickerOffersNothingWithNoTerrainSelected) {
  tileset_.terrains = {MakeTerrain(3, "Grass", 7)};
  PreselectTileset();

  // A shape means nothing without a material to make it out of.
  EXPECT_EQ(RenderCountingShapeGlyphs(), 0);
}

TEST_F(TerrainPalettePanelTest, ASelectedTerrainOffersTheShapesItCanPaint) {
  tileset_.tiles = {Tile{.id = 7, .name = "solid", .shape = TileShape::kFullBlock}};
  tileset_.terrains = {MakeTerrain(3, "Grass", 7)};
  PreselectTileset();
  // Clicked once to select, then left alone while the picker is inspected.
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true)).WillRepeatedly(Return(false));
  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());

  // Artwork for one shape, so one glyph. The picker offers what can be painted
  // rather than the whole catalogue.
  EXPECT_EQ(RenderCountingShapeGlyphs(), 1);
}

// Twenty-five named entries in a dropdown was the thing that made this palette
// unreadable. Every shape a terrain can paint gets its own clickable glyph.
TEST_F(TerrainPalettePanelTest, ADerivedTerrainOffersEveryShapeAsItsOwnGlyph) {
  tileset_.tiles = {};
  Terrain derived;
  derived.id = 3;
  derived.name = "Cave";
  derived.scheme = TerrainScheme::kDerived;
  tileset_.terrains = {std::move(derived)};
  PreselectTileset();
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true)).WillRepeatedly(Return(false));
  ASSERT_TRUE(Render().ok());
  ASSERT_TRUE(panel_->GetSelectedTerrainId().has_value());

  EXPECT_EQ(RenderCountingShapeGlyphs(), static_cast<int>(AllTerrainShapeChoices().size()))
      << "a derived terrain renders on demand, so it can paint the whole catalogue";
}

TEST_F(TerrainPalettePanelTest, AShapeTheNewTerrainCannotPaintIsNotLeftSelected) {
  // Selecting a different terrain can strand a shape. Leaving the brush pointed
  // at geometry with no artwork behind it would make the next paint fail on a
  // choice the user never made.
  tileset_.tiles = {
      Tile{.id = 7, .name = "solid", .shape = TileShape::kFullBlock},
      Tile{.id = 8, .name = "ramp", .shape = TileShape::kSlope45BottomLeft},
  };
  Terrain slopes = MakeTerrain(3, "Grass", 7);
  slopes.shape_tile_ids = {8};
  tileset_.terrains = {std::move(slopes)};
  PreselectTileset();
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true)).WillRepeatedly(Return(false));
  ASSERT_TRUE(Render().ok());

  TerrainPalettePanelTestPeer::SetSelectedShape(*panel_, TileShape::kSlope45BottomLeft);
  // A terrain with only block artwork replaces it.
  tileset_.terrains = {MakeTerrain(3, "Grass", 7)};

  ASSERT_TRUE(Render().ok());

  EXPECT_EQ(panel_->GetSelectedShape(), TileShape::kFullBlock);
}

TEST_F(TerrainPalettePanelTest, ADerivedTerrainOffersShapesItHasNoArtworkForYet) {
  // The point of deriving artwork: a shape is paintable because the recipe can
  // render it, not because a tile for it already exists.
  tileset_.tiles = {};
  Terrain derived;
  derived.id = 3;
  derived.name = "Cave";
  derived.scheme = TerrainScheme::kDerived;
  tileset_.terrains = {std::move(derived)};
  PreselectTileset();
  EXPECT_CALL(gui_, IsItemClicked(0)).WillOnce(Return(true)).WillRepeatedly(Return(false));
  ASSERT_TRUE(Render().ok());

  TerrainPalettePanelTestPeer::SetSelectedShape(*panel_, TileShape::kSteepSlopeTopRightTop);

  ASSERT_TRUE(Render().ok());

  EXPECT_EQ(panel_->GetSelectedShape(), TileShape::kSteepSlopeTopRightTop)
      << "a derived terrain can render any shape, so none should be stranded";
}

}  // namespace
}  // namespace zebes
