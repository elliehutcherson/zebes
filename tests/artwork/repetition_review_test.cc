#include "artwork/repetition_review.h"

#include <cstdint>
#include <limits>
#include <vector>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(RepetitionReviewTest, BuildsCompleteTiledPreview) {
  const RgbaImage source{
      .width = 2,
      .height = 1,
      .pixels = {1, 2, 3, 255, 4, 5, 6, 255},
  };

  ASSERT_OK_AND_ASSIGN(const RgbaImage preview, BuildRepetitionPreview(source, 2, 2, 8));

  EXPECT_EQ(preview.width, 4);
  EXPECT_EQ(preview.height, 2);
  EXPECT_EQ(preview.pixels,
            (std::vector<uint8_t>{1, 2, 3, 255, 4, 5, 6, 255, 1, 2, 3, 255, 4, 5, 6, 255,
                                  1, 2, 3, 255, 4, 5, 6, 255, 1, 2, 3, 255, 4, 5, 6, 255}));
}

TEST(RepetitionReviewTest, RejectsOversizedDimensionsBeforeMultiplyingThem) {
  const RgbaImage source{
      .width = 2,
      .height = 2,
      .pixels = std::vector<uint8_t>(16, 255),
  };

  const absl::Status status = BuildRepetitionPreview(source, std::numeric_limits<int>::max(),
                                                     std::numeric_limits<int>::max(), 100)
                                  .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
}

TEST(RepetitionReviewTest, RejectsPreviewAbovePixelLimit) {
  const RgbaImage source{
      .width = 2,
      .height = 2,
      .pixels = std::vector<uint8_t>(16, 255),
  };

  EXPECT_EQ(BuildRepetitionPreview(source, 2, 2, 15).status().code(),
            absl::StatusCode::kResourceExhausted);
}

}  // namespace
}  // namespace zebes
