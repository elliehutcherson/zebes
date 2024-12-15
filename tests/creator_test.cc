#include "engine/creator.h"

#include "absl/status/status_matchers.h"
#include "camera_mock.h"
#include "engine/camera_interface.h"
#include "engine/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

using ::absl_testing::StatusIs;

class CreatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(GameConfig config, GameConfig::Create());
    config_ = std::make_unique<GameConfig>(std::move(config));
    // Any common setup for the tests
    camera_mock_ = std::make_unique<CameraMock>();
    ASSERT_OK_AND_ASSIGN(creator_,
                         Creator::Create({config_.get(), camera_mock_.get()}));
  }

  void TearDown() override {
    // Any cleanup needed after the tests
    camera_mock_.reset();
  }

  std::unique_ptr<GameConfig> config_;
  std::unique_ptr<CameraMock> camera_mock_;
  std::unique_ptr<Creator> creator_;
};

TEST_F(CreatorTest, SaveSucceeds) { EXPECT_OK(creator_->SaveConfig(*config_)); }

TEST_F(CreatorTest, ImportFails) {
  EXPECT_THAT(creator_->ImportConfig("non_existent_file").status(),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(CreatorTest, ImportSucceeds) {
  ASSERT_OK(creator_->SaveConfig(*config_));

  ASSERT_OK_AND_ASSIGN(const GameConfig imported_config,
                       creator_->ImportConfig(config_->paths.config()));
}

}  // namespace
}  // namespace zebes

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}