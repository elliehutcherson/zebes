#include "scripts/animation_artwork_run_manifest.h"

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "common/utils.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

class AnimationArtworkRunManifestTest : public ::testing::Test {
 protected:
  AnimationArtworkRunManifestTest()
      : root_(std::filesystem::temp_directory_path() /
              std::filesystem::path("zebes-animation-run-manifest-" + GenerateGuid())) {}

  ~AnimationArtworkRunManifestTest() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  void WriteManifest(std::string_view timing_json) {
    ASSERT_TRUE(std::filesystem::create_directories(root_));
    std::ofstream stream(manifest_path());
    ASSERT_TRUE(stream.is_open());
    stream << R"json({
  "schema_version": 1,
  "clip": "locomotion-right",
  "sheet": {
    "grid_x": 0,
    "grid_y": 0,
    "cell_width": 1024,
    "cell_height": 1024,
    "column_gap": 0,
    "row_gap": 0,
    "columns": 6,
    "rows": 2
  },
  "timing": )json"
           << timing_json << R"json(,
  "planted_frames": [
    true, false, false, true, true, false,
    false, true, true, false, false, true
  ]
})json";
    stream.close();
    ASSERT_TRUE(stream);
  }

  std::filesystem::path manifest_path() const { return root_ / "run-manifest.json"; }

 private:
  std::filesystem::path root_;
};

TEST_F(AnimationArtworkRunManifestTest, ExpandsUniformTimingAndPreservesOrderedPlantedFrames) {
  WriteManifest(R"json({"uniform_frames_per_cycle": 4})json");

  ASSERT_OK_AND_ASSIGN(const AnimationArtworkRunManifest manifest,
                       LoadAnimationArtworkRunManifest(manifest_path()));

  EXPECT_EQ(manifest.sheet.columns, 6);
  EXPECT_EQ(manifest.sheet.rows, 2);
  EXPECT_EQ(manifest.sheet.cell_width, manifest.sheet.cell_height);
  EXPECT_EQ(manifest.frames_per_cycle, std::vector<int>(12, 4));
  EXPECT_EQ(manifest.planted_frames, (std::vector<bool>{true, false, false, true, true, false,
                                                        false, true, true, false, false, true}));
}

TEST_F(AnimationArtworkRunManifestTest, RejectsBareTimingArray) {
  WriteManifest(R"json([4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4])json");

  const absl::Status status = LoadAnimationArtworkRunManifest(manifest_path()).status();

  EXPECT_TRUE(absl::IsInvalidArgument(status));
}

}  // namespace
}  // namespace zebes
