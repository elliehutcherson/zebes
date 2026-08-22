#include "editor/anchor_gizmo.h"

#include <limits>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(AnchorGizmoTest, FreeModeDrawsOnlyOriginCross) {
  ASSERT_OK_AND_ASSIGN(const AnchorGizmoGeometry geometry,
                       CalculateAnchorGizmoGeometry({10, 20}, BlueprintPlacementMode::kFree));

  ASSERT_EQ(geometry.origin.size(), 2u);
  EXPECT_EQ(geometry.origin[0], (AnchorGizmoLine{.start = {4, 20}, .end = {16, 20}}));
  EXPECT_EQ(geometry.origin[1], (AnchorGizmoLine{.start = {10, 14}, .end = {10, 26}}));
  EXPECT_TRUE(geometry.surface.empty());
}

TEST(AnchorGizmoTest, GroundedSurfaceTicksPointTowardArtwork) {
  ASSERT_OK_AND_ASSIGN(const AnchorGizmoGeometry geometry,
                       CalculateAnchorGizmoGeometry({10, 20}, BlueprintPlacementMode::kGrounded));

  ASSERT_EQ(geometry.surface.size(), 4u);
  EXPECT_EQ(geometry.surface[2], (AnchorGizmoLine{.start = {-3, 20}, .end = {-3, 16}}));
  EXPECT_EQ(geometry.surface[3], (AnchorGizmoLine{.start = {23, 20}, .end = {23, 16}}));
}

TEST(AnchorGizmoTest, CeilingSurfaceTicksPointTowardArtwork) {
  ASSERT_OK_AND_ASSIGN(const AnchorGizmoGeometry geometry,
                       CalculateAnchorGizmoGeometry({10, 20}, BlueprintPlacementMode::kCeiling));

  ASSERT_EQ(geometry.surface.size(), 4u);
  EXPECT_EQ(geometry.surface[2], (AnchorGizmoLine{.start = {-3, 20}, .end = {-3, 24}}));
  EXPECT_EQ(geometry.surface[3], (AnchorGizmoLine{.start = {23, 20}, .end = {23, 24}}));
}

TEST(AnchorGizmoTest, UnknownAttachmentStillShowsAnOriginWithoutInventingASurface) {
  ASSERT_OK_AND_ASSIGN(const AnchorGizmoGeometry geometry,
                       CalculateAnchorGizmoGeometry({0, 0}, std::nullopt));
  EXPECT_EQ(geometry.origin.size(), 2u);
  EXPECT_TRUE(geometry.surface.empty());
}

TEST(AnchorGizmoTest, RejectsInvalidGeometryInputs) {
  EXPECT_EQ(CalculateAnchorGizmoGeometry({std::numeric_limits<double>::infinity(), 0},
                                         BlueprintPlacementMode::kFree)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      CalculateAnchorGizmoGeometry({0, 0}, static_cast<BlueprintPlacementMode>(99)).status().code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(CalculateAnchorGizmoGeometry(
                {0, 0}, BlueprintPlacementMode::kFree,
                {.origin_arm_length = 6, .surface_half_width = 6, .surface_tick_length = 4})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
