#include "editor/palette_ui.h"

#include <limits>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(PaletteGridLayoutTest, PacksItemsUsingSharedGapSemantics) {
  ASSERT_OK_AND_ASSIGN(const PaletteGridLayout layout,
                       CalculatePaletteGridLayout(268.0f, 84.0f, 8.0f));
  EXPECT_EQ(layout.columns, 3);
  EXPECT_TRUE(layout.ContinueRowAfter(1));
  EXPECT_TRUE(layout.ContinueRowAfter(2));
  EXPECT_FALSE(layout.ContinueRowAfter(3));
}

TEST(PaletteGridLayoutTest, KeepsOneColumnWhenViewportIsNarrow) {
  ASSERT_OK_AND_ASSIGN(const PaletteGridLayout layout,
                       CalculatePaletteGridLayout(10.0f, 84.0f, 8.0f));
  EXPECT_EQ(layout.columns, 1);
  EXPECT_FALSE(layout.ContinueRowAfter(1));
}

TEST(PaletteGridLayoutTest, RejectsInvalidDimensions) {
  EXPECT_EQ(CalculatePaletteGridLayout(-1.0f, 84.0f, 8.0f).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(CalculatePaletteGridLayout(100.0f, 0.0f, 8.0f).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(CalculatePaletteGridLayout(std::numeric_limits<float>::infinity(), 84.0f, 8.0f)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
