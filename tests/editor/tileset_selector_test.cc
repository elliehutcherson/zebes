#include "editor/level_editor/tileset_selector.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/tileset.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

TEST(TilesetSelectorTest, ResolvesStableIdEachFrameAndClearsMissingSelection) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  TilesetSelector selector;
  Tileset stable{.id = "cave", .name = "Cave"};

  ON_CALL(gui, CreateScopedCombo(_, _, _))
      .WillByDefault(Invoke([&gui](const char* label, const char* preview, ImGuiComboFlags) {
        return ScopedCombo(&gui, label, preview);
      }));
  ON_CALL(gui, BeginCombo(_, _, _)).WillByDefault(Return(false));
  ON_CALL(api, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{stable}));
  ON_CALL(api, GetTileset("cave")).WillByDefault(Return(&stable));

  selector.Select("cave");
  ASSERT_OK_AND_ASSIGN(const TilesetSelectorResult selected,
                       selector.Render(api, gui, "Tileset", "test_"));
  EXPECT_EQ(selected.tileset, &stable);
  EXPECT_FALSE(selected.selection_changed);

  ON_CALL(api, GetAllTilesets()).WillByDefault(Return(std::vector<Tileset>{}));
  ASSERT_OK_AND_ASSIGN(const TilesetSelectorResult removed,
                       selector.Render(api, gui, "Tileset", "test_"));
  EXPECT_EQ(removed.tileset, nullptr);
  EXPECT_TRUE(removed.selection_changed);
  EXPECT_TRUE(removed.catalog_empty);
  EXPECT_FALSE(selector.selected_id().has_value());
}

TEST(TilesetSelectorTest, RejectsMissingWidgetIdentity) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  TilesetSelector selector;

  EXPECT_FALSE(selector.Render(api, gui, nullptr, "test_").ok());
  EXPECT_FALSE(selector.Render(api, gui, "Tileset", "").ok());
}

}  // namespace
}  // namespace zebes
