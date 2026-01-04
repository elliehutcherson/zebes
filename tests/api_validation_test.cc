#include "api/api.h"
#include "blueprint_manager_mock.h"
#include "collider_manager_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "level_manager_mock.h"
#include "macros.h"
#include "sprite_manager_mock.h"
#include "texture_manager_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::Return;

class ApiValidationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Api::Options options = {
        .config = &config_,
        .texture_manager = &texture_manager_,
        .sprite_manager = &sprite_manager_,
        .collider_manager = &collider_manager_,
        .blueprint_manager = &blueprint_manager_,
        .level_manager = &level_manager_,
    };

    ASSERT_OK_AND_ASSIGN(api_, Api::Create(options));
  }

  GameConfig config_;
  TextureManagerMock texture_manager_;
  SpriteManagerMock sprite_manager_;
  ColliderManagerMock collider_manager_;
  BlueprintManagerMock blueprint_manager_;
  LevelManagerMock level_manager_;
  std::unique_ptr<Api> api_;
};

TEST_F(ApiValidationTest, DeleteTexture_InUse_ReturnsError) {
  std::string texture_id = "test_texture";

  EXPECT_CALL(sprite_manager_, IsTextureUsed(texture_id)).WillOnce(Return(true));
  EXPECT_CALL(texture_manager_, DeleteTexture(_)).Times(0);

  auto status = api_->DeleteTexture(texture_id);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteTexture_NotInUse_CallsDelete) {
  std::string texture_id = "test_texture";

  EXPECT_CALL(sprite_manager_, IsTextureUsed(texture_id)).WillOnce(Return(false));
  EXPECT_CALL(texture_manager_, DeleteTexture(texture_id)).WillOnce(Return(absl::OkStatus()));

  auto status = api_->DeleteTexture(texture_id);
  EXPECT_TRUE(status.ok());
}

TEST_F(ApiValidationTest, DeleteSprite_InUseInBlueprint_ReturnsError) {
  std::string sprite_id = "test_sprite";

  EXPECT_CALL(blueprint_manager_, IsSpriteUsed(sprite_id)).WillOnce(Return(true));
  EXPECT_CALL(sprite_manager_, DeleteSprite(_)).Times(0);

  auto status = api_->DeleteSprite(sprite_id);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteSprite_NotInUse_CallsDelete) {
  std::string sprite_id = "test_sprite";

  EXPECT_CALL(blueprint_manager_, IsSpriteUsed(sprite_id)).WillOnce(Return(false));
  EXPECT_CALL(sprite_manager_, DeleteSprite(sprite_id)).WillOnce(Return(absl::OkStatus()));

  auto status = api_->DeleteSprite(sprite_id);
  EXPECT_TRUE(status.ok());
}

TEST_F(ApiValidationTest, DeleteCollider_InUseInBlueprint_ReturnsError) {
  std::string collider_id = "test_collider";

  EXPECT_CALL(blueprint_manager_, IsColliderUsed(collider_id)).WillOnce(Return(true));
  EXPECT_CALL(collider_manager_, DeleteCollider(_)).Times(0);

  auto status = api_->DeleteCollider(collider_id);
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ApiValidationTest, DeleteCollider_NotInUse_CallsDelete) {
  std::string collider_id = "test_collider";

  EXPECT_CALL(blueprint_manager_, IsColliderUsed(collider_id)).WillOnce(Return(false));
  EXPECT_CALL(collider_manager_, DeleteCollider(collider_id)).WillOnce(Return(absl::OkStatus()));

  auto status = api_->DeleteCollider(collider_id);
  EXPECT_TRUE(status.ok());
}

}  // namespace
}  // namespace zebes
