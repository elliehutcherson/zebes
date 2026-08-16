#include "editor/tileset_editor/tileset_editor.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/tileset.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"
#include "macros.h"

namespace zebes {

// The atlas gestures live on the view, not the model: the model owns what a
// region means, while the view owns when a press and a release add up to one.
// Reaching them the way LevelEditorTestPeer does is what makes that half
// testable without a window.
class TilesetEditorTestPeer {
 public:
  static TilesetEditorModel& GetModel(TilesetEditor& editor) { return editor.model_; }

  static absl::Status HandleAtlasInteraction(TilesetEditor& editor, int texture_width,
                                             int texture_height) {
    return editor.HandleAtlasInteraction(/*draw_list=*/nullptr, texture_width, texture_height);
  }

  static const std::string& ViewportStatus(const TilesetEditor& editor) {
    return editor.viewport_status_;
  }

  // The gesture reads the cursor through the canvas, so the test has to drive
  // the real canvas rather than assume a mapping. Exposing both lets the
  // fixture convert atlas coordinates into the screen positions that would
  // actually produce them, whatever the camera has clamped itself to.
  static Canvas& GetCanvas(TilesetEditor& editor) { return editor.canvas_; }
  static Camera& GetCamera(TilesetEditor& editor) { return editor.camera_; }
};

namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

constexpr int kAtlasWidth = 128;
constexpr int kAtlasHeight = 128;

class TilesetEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<TilesetEditor>> editor = TilesetEditor::Create(&api_, &gui_);
    ASSERT_OK(editor);
    editor_ = *std::move(editor);

    model().BeginNewTileset();

    ON_CALL(gui_, IsItemHovered(_)).WillByDefault(Invoke([this](ImGuiHoveredFlags) {
      return hovered_;
    }));
    ON_CALL(gui_, IsItemActive()).WillByDefault(Invoke([this] { return held_; }));
    ON_CALL(gui_, IsItemClicked(_)).WillByDefault(Invoke([this](ImGuiMouseButton) {
      return pressed_;
    }));
    ON_CALL(gui_, GetMousePos()).WillByDefault(Invoke([this] { return cursor_; }));
    ON_CALL(gui_, GetCursorScreenPos()).WillByDefault(Return(ImVec2(0, 0)));

    // The gesture resolves the cursor through the canvas, so the canvas has to
    // be live. Begin installs the camera and the ruler offsets that
    // ScreenToWorld depends on.
    Canvas& canvas = TilesetEditorTestPeer::GetCanvas(*editor_);
    canvas.SetWorldBounds({0, 0}, {kAtlasWidth, kAtlasHeight});
    canvas.Begin("TestAtlas", ImVec2(400, 400), TilesetEditorTestPeer::GetCamera(*editor_));
  }

  void TearDown() override { TilesetEditorTestPeer::GetCanvas(*editor_).End(); }

  TilesetEditorModel& model() { return TilesetEditorTestPeer::GetModel(*editor_); }

  // One frame of atlas input over the given atlas coordinate. Converting
  // through the canvas rather than assuming a mapping keeps the test honest
  // about whatever zoom the camera clamped itself to.
  void Frame(float x, float y) {
    cursor_ = TilesetEditorTestPeer::GetCanvas(*editor_).WorldToScreen({x, y});
    const absl::Status status =
        TilesetEditorTestPeer::HandleAtlasInteraction(*editor_, kAtlasWidth, kAtlasHeight);
    ASSERT_OK(status);
  }

  // Press at one cell, drag through the rest, release. Mirrors the frame
  // sequence ImGui produces for a real drag.
  void DragFrom(float x0, float y0, float x1, float y1) {
    pressed_ = true;
    held_ = true;
    Frame(x0, y0);
    pressed_ = false;
    Frame(x1, y1);
    held_ = false;
    Frame(x1, y1);
  }

  void ClickAt(float x, float y) { DragFrom(x, y, x, y); }

  const std::string& status() const { return TilesetEditorTestPeer::ViewportStatus(*editor_); }

  NiceMock<MockApi> api_;
  NiceMock<MockGui> gui_;
  std::unique_ptr<TilesetEditor> editor_;
  ImVec2 cursor_{0, 0};
  bool hovered_ = true;
  bool held_ = false;
  bool pressed_ = false;
};

// Cutting an atlas by hand cost roughly four interactions per tile, which is
// why a forty-cell sheet was not worth doing.
TEST_F(TilesetEditorTest, DraggingAcrossCellsAddsATilePerCell) {
  DragFrom(0.0f, 0.0f, 64.0f, 32.0f);

  ASSERT_NE(model().active_tileset(), nullptr);
  EXPECT_EQ(model().active_tileset()->tiles.size(), 6u);
  EXPECT_THAT(status(), ::testing::HasSubstr("6"));
}

// A press and a release on one cell is the gesture this viewport has always
// had. The two can only be told apart once the button comes up, which is why
// the whole thing resolves on release.
TEST_F(TilesetEditorTest, ClickingOneCellRepointsTheSelectedTile) {
  ASSERT_OK(model().AddTile());
  const int tile_id = model().selected_tile_id();

  ClickAt(64.0f, 32.0f);

  EXPECT_EQ(model().active_tileset()->tiles.size(), 1u) << "a click added a tile";
  ASSERT_NE(model().selected_tile(), nullptr);
  EXPECT_EQ(model().selected_tile()->id, tile_id);
  EXPECT_EQ(model().selected_tile()->source_x, 64);
  EXPECT_EQ(model().selected_tile()->source_y, 32);
}

// With nothing selected there is nothing to re-point, and silently adding a
// tile instead would make a stray click destructive.
TEST_F(TilesetEditorTest, ClickingOneCellWithNothingSelectedSaysSo) {
  ClickAt(0.0f, 0.0f);

  EXPECT_TRUE(model().active_tileset()->tiles.empty());
  EXPECT_THAT(status(), ::testing::HasSubstr("Select a tile"));
}

// Nothing is written until the button comes up, so a drag in progress can still
// be steered or abandoned.
TEST_F(TilesetEditorTest, ADragInProgressAddsNothingYet) {
  pressed_ = true;
  held_ = true;
  Frame(0.0f, 0.0f);
  pressed_ = false;
  Frame(64.0f, 32.0f);

  EXPECT_TRUE(model().active_tileset()->tiles.empty());
}

// Leaving the atlas mid-drag is normal -- the cursor overshoots the edge -- and
// must commit the last cell actually covered rather than nothing.
TEST_F(TilesetEditorTest, ADragThatLeavesTheAtlasCommitsTheLastValidCell) {
  pressed_ = true;
  held_ = true;
  Frame(0.0f, 0.0f);
  pressed_ = false;
  Frame(32.0f, 0.0f);

  // Off the edge: no cell under the cursor at all.
  hovered_ = false;
  Frame(500.0f, 0.0f);
  held_ = false;
  Frame(500.0f, 0.0f);

  EXPECT_EQ(model().active_tileset()->tiles.size(), 2u);
}

// A press that never landed on a cell has no anchor, so releasing does nothing
// rather than acting on whatever the cursor last touched.
TEST_F(TilesetEditorTest, APressOutsideTheAtlasStartsNothing) {
  hovered_ = false;
  pressed_ = true;
  held_ = true;
  Frame(500.0f, 500.0f);
  pressed_ = false;
  held_ = false;
  hovered_ = true;
  Frame(0.0f, 0.0f);

  EXPECT_TRUE(model().active_tileset()->tiles.empty());
}

TEST_F(TilesetEditorTest, DraggingBackOverExistingTilesAddsOnlyWhatIsMissing) {
  DragFrom(0.0f, 0.0f, 32.0f, 0.0f);
  ASSERT_EQ(model().active_tileset()->tiles.size(), 2u);

  DragFrom(0.0f, 0.0f, 64.0f, 0.0f);

  EXPECT_EQ(model().active_tileset()->tiles.size(), 3u);
  EXPECT_THAT(status(), ::testing::HasSubstr("1"));
}

}  // namespace
}  // namespace zebes
