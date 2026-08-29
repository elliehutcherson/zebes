#include "engine/tile_collision.h"

#include <cmath>
#include <optional>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr Vec kTileSize{32.0, 32.0};

TEST(TileCollisionTest, ReportsGroundWallAndCeilingSeparation) {
  ASSERT_OK_AND_ASSIGN(
      const std::optional<TileCollisionContact> ground,
      IntersectBoxWithTileShape({.min = {8.0, 0.0}, .max = {24.0, 34.0}}, TileShape::kFullBlock,
                                {.x = 0.0, .y = 32.0}, kTileSize));
  ASSERT_TRUE(ground.has_value());
  EXPECT_DOUBLE_EQ(ground->normal.x, 0.0);
  EXPECT_DOUBLE_EQ(ground->normal.y, -1.0);
  EXPECT_DOUBLE_EQ(ground->penetration, 2.0);

  ASSERT_OK_AND_ASSIGN(
      const std::optional<TileCollisionContact> wall,
      IntersectBoxWithTileShape({.min = {29.0, 8.0}, .max = {35.0, 24.0}}, TileShape::kFullBlock,
                                {.x = 32.0, .y = 0.0}, kTileSize));
  ASSERT_TRUE(wall.has_value());
  EXPECT_DOUBLE_EQ(wall->normal.x, -1.0);
  EXPECT_DOUBLE_EQ(wall->normal.y, 0.0);
  EXPECT_DOUBLE_EQ(wall->penetration, 3.0);

  ASSERT_OK_AND_ASSIGN(
      const std::optional<TileCollisionContact> ceiling,
      IntersectBoxWithTileShape({.min = {8.0, 30.0}, .max = {24.0, 60.0}}, TileShape::kFullBlock,
                                {.x = 0.0, .y = 0.0}, kTileSize));
  ASSERT_TRUE(ceiling.has_value());
  EXPECT_DOUBLE_EQ(ceiling->normal.x, 0.0);
  EXPECT_DOUBLE_EQ(ceiling->normal.y, 1.0);
  EXPECT_DOUBLE_EQ(ceiling->penetration, 2.0);
}

TEST(TileCollisionTest, UsesTileShapeGeometryForSlopeContact) {
  ASSERT_OK_AND_ASSIGN(const std::optional<TileCollisionContact> contact,
                       IntersectBoxWithTileShape({.min = {16.0, 8.0}, .max = {24.0, 16.0}},
                                                 TileShape::kSlope45FloorTallRight,
                                                 {.x = 0.0, .y = 0.0}, kTileSize));

  ASSERT_TRUE(contact.has_value());
  constexpr double kInverseSqrtTwo = 0.7071067811865475;
  EXPECT_NEAR(contact->normal.x, -kInverseSqrtTwo, 1e-12);
  EXPECT_NEAR(contact->normal.y, -kInverseSqrtTwo, 1e-12);
  EXPECT_NEAR(contact->penetration, 8.0 * kInverseSqrtTwo, 1e-12);
}

TEST(TileCollisionTest, TreatsAirSeparationAndTouchingAsNoContact) {
  ASSERT_OK_AND_ASSIGN(
      const std::optional<TileCollisionContact> air,
      IntersectBoxWithTileShape({.min = {0.0, 0.0}, .max = {16.0, 16.0}}, TileShape::kNone,
                                {.x = 0.0, .y = 0.0}, kTileSize));
  EXPECT_FALSE(air.has_value());

  ASSERT_OK_AND_ASSIGN(
      const std::optional<TileCollisionContact> separated,
      IntersectBoxWithTileShape({.min = {0.0, 0.0}, .max = {16.0, 16.0}}, TileShape::kFullBlock,
                                {.x = 32.0, .y = 32.0}, kTileSize));
  EXPECT_FALSE(separated.has_value());

  ASSERT_OK_AND_ASSIGN(
      const std::optional<TileCollisionContact> touching,
      IntersectBoxWithTileShape({.min = {8.0, 0.0}, .max = {24.0, 32.0}}, TileShape::kFullBlock,
                                {.x = 0.0, .y = 32.0}, kTileSize));
  EXPECT_FALSE(touching.has_value());
}

TEST(TileCollisionTest, SeparatesABoxContainedByATile) {
  const AxisAlignedBox box{.min = {8.0, 8.0}, .max = {24.0, 24.0}};
  ASSERT_OK_AND_ASSIGN(
      const std::optional<TileCollisionContact> contact,
      IntersectBoxWithTileShape(box, TileShape::kFullBlock, {.x = 0.0, .y = 0.0}, kTileSize));
  ASSERT_TRUE(contact.has_value());

  const Vec translation = {
      .x = contact->normal.x * contact->penetration,
      .y = contact->normal.y * contact->penetration,
  };
  const AxisAlignedBox separated_box{
      .min = {.x = box.min.x + translation.x, .y = box.min.y + translation.y},
      .max = {.x = box.max.x + translation.x, .y = box.max.y + translation.y},
  };
  ASSERT_OK_AND_ASSIGN(const std::optional<TileCollisionContact> separated,
                       IntersectBoxWithTileShape(separated_box, TileShape::kFullBlock,
                                                 {.x = 0.0, .y = 0.0}, kTileSize));
  EXPECT_FALSE(separated.has_value());
}

TEST(TileCollisionTest, RejectsInvalidGeometryAndShape) {
  EXPECT_EQ(IntersectBoxWithTileShape({.min = {1.0, 0.0}, .max = {1.0, 2.0}}, TileShape::kFullBlock,
                                      {.x = 0.0, .y = 0.0}, kTileSize)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(IntersectBoxWithTileShape({.min = {0.0, 0.0}, .max = {1.0, 2.0}}, TileShape::kFullBlock,
                                      {.x = 0.0, .y = 0.0}, {.x = 0.0, .y = 32.0})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(IntersectBoxWithTileShape({.min = {0.0, 0.0}, .max = {1.0, 2.0}},
                                      static_cast<TileShape>(255), {.x = 0.0, .y = 0.0}, kTileSize)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
