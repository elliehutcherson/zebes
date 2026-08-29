#include "engine/collision.h"

#include <array>
#include <optional>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr std::array<Vec, 5> kConvexPentagon = {
    Vec{.x = 0.0, .y = 8.0},  Vec{.x = 8.0, .y = 0.0},   Vec{.x = 24.0, .y = 0.0},
    Vec{.x = 32.0, .y = 8.0}, Vec{.x = 16.0, .y = 24.0},
};

TEST(CollisionTest, SweepsAgainstGeneralConvexGeometry) {
  ASSERT_OK_AND_ASSIGN(const std::optional<CollisionSweepContact> contact,
                       SweepBoxWithConvexPolygon({.min = {-16.0, 8.0}, .max = {-8.0, 16.0}},
                                                 {.x = 32.0, .y = 0.0}, kConvexPentagon));

  ASSERT_TRUE(contact.has_value());
  EXPECT_GT(contact->time, 0.0);
  EXPECT_LT(contact->time, 1.0);
  EXPECT_LT(contact->normal.x, 0.0);
}

TEST(CollisionTest, SharedGeometryHelpersTranslateAndProjectMotion) {
  const AxisAlignedBox box{.min = {1.0, 2.0}, .max = {3.0, 4.0}};
  EXPECT_EQ(TranslateBox(box, {.x = 5.0, .y = 7.0}),
            (AxisAlignedBox{.min = {6.0, 9.0}, .max = {8.0, 11.0}}));
  EXPECT_EQ(RemoveInwardComponent({.x = 4.0, .y = 6.0}, {.x = 0.0, .y = -1.0}),
            (Vec{.x = 4.0, .y = 0.0}));
}

TEST(CollisionTest, RejectsConcaveGeometry) {
  constexpr std::array<Vec, 5> kConcave = {
      Vec{.x = 0.0, .y = 0.0}, Vec{.x = 4.0, .y = 0.0}, Vec{.x = 2.0, .y = 1.0},
      Vec{.x = 4.0, .y = 4.0}, Vec{.x = 0.0, .y = 4.0},
  };

  EXPECT_TRUE(absl::IsInvalidArgument(
      IntersectBoxWithConvexPolygon({.min = {1.0, 1.0}, .max = {2.0, 2.0}}, kConcave).status()));
}

}  // namespace
}  // namespace zebes
