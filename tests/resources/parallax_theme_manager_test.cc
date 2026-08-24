#include "resources/parallax_theme_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

ParallaxTheme CompleteTheme() {
  return {
      .name = "Crystal Cave",
      .layers = {{
          .name = "Far Formations",
          .scroll_factor = {0.2, 0.1},
          .offset = {-8.0, 4.0},
          .repeat_period = {640.0, 0.0},
          .elements = {{.id = 0,
                        .name = "Far A",
                        .texture_id = "texture-1",
                        .position = {16.0, -4.0},
                        .scale = 2.0f}},
      }},
  };
}

class ParallaxThemeManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("zebes-parallax-themes-" + std::to_string(++sequence_));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, ParallaxThemeManager::Create(path_.string()));
    ASSERT_OK(manager_->LoadAllThemes());
  }
  void TearDown() override { std::filesystem::remove_all(path_); }

  static int sequence_;
  std::filesystem::path path_;
  std::unique_ptr<ParallaxThemeManager> manager_;
};

int ParallaxThemeManagerTest::sequence_ = 0;

TEST_F(ParallaxThemeManagerTest, RoundTripsACompleteTheme) {
  ParallaxTheme expected = CompleteTheme();
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateTheme(expected));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ParallaxThemeManager> reloaded,
                       ParallaxThemeManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllThemes());
  ASSERT_OK_AND_ASSIGN(ParallaxTheme * actual, reloaded->GetTheme(id));
  expected.id = id;
  EXPECT_EQ(*actual, expected);
}

TEST_F(ParallaxThemeManagerTest, RefusesAnIncompleteThemeWithoutPublishingIt) {
  ParallaxTheme theme = CompleteTheme();
  theme.layers[0].elements[0].texture_id.clear();
  EXPECT_EQ(manager_->CreateTheme(theme).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(manager_->GetAllThemes().empty());
}

TEST_F(ParallaxThemeManagerTest, RefusesFilenameIdentityMismatchOnLoad) {
  ParallaxTheme theme = CompleteTheme();
  theme.id = "inside";
  const std::filesystem::path directory = path_ / "definitions/parallax_themes";
  std::filesystem::create_directories(directory);
  std::ofstream(directory / "outside.json") << ParallaxThemeToJson(theme).dump(2);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ParallaxThemeManager> reloaded,
                       ParallaxThemeManager::Create(path_.string()));
  EXPECT_EQ(reloaded->LoadAllThemes().code(), absl::StatusCode::kDataLoss);
}

TEST_F(ParallaxThemeManagerTest, SavingPreservesPointers) {
  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateTheme(CompleteTheme()));
  ASSERT_OK_AND_ASSIGN(ParallaxTheme * held, manager_->GetTheme(id));
  ParallaxTheme edited = *held;
  edited.name = "Shared Crystal Cave";
  ASSERT_OK(manager_->SaveTheme(edited));
  ASSERT_OK_AND_ASSIGN(ParallaxTheme * after, manager_->GetTheme(id));
  EXPECT_EQ(after, held);
  EXPECT_EQ(after->name, edited.name);
}

TEST_F(ParallaxThemeManagerTest, DuplicatedThemeHasIndependentIdentityAndContents) {
  ASSERT_OK_AND_ASSIGN(const std::string original_id, manager_->CreateTheme(CompleteTheme()));
  ASSERT_OK_AND_ASSIGN(ParallaxTheme * original, manager_->GetTheme(original_id));

  ParallaxTheme duplicate = *original;
  duplicate.name = "Crystal Cave Copy";
  ASSERT_OK_AND_ASSIGN(const std::string duplicate_id, manager_->CreateTheme(duplicate));
  EXPECT_NE(duplicate_id, original_id);

  ASSERT_OK_AND_ASSIGN(ParallaxTheme * copy, manager_->GetTheme(duplicate_id));
  copy->layers[0].offset.x = 42.0;
  ASSERT_OK(manager_->SaveTheme(*copy));

  ASSERT_OK_AND_ASSIGN(original, manager_->GetTheme(original_id));
  EXPECT_EQ(original->layers[0].offset.x, -8.0);
  EXPECT_EQ(copy->layers[0].offset.x, 42.0);
}

}  // namespace
}  // namespace zebes
