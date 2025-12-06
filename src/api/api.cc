#include "api/api.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Api>> Api::Create(const Options& options) {
  if (options.config == nullptr) {
    return absl::InvalidArgumentError("GameConfig is null.");
  }
  if (options.db == nullptr) {
    return absl::InvalidArgumentError("DbInterface is null.");
  }
  return std::unique_ptr<Api>(new Api(options));
}

Api::Api(const Options& options) : config_(options.config), db_(options.db) {}

absl::StatusOr<std::string> Api::CreateTexture(const std::string& texture_path) {
  // Validate input
  if (!std::filesystem::exists(texture_path)) {
    return absl::NotFoundError(absl::StrCat("Source texture file does not exist: ", texture_path));
  }

  // Generate destination paths
  const std::string filename = std::filesystem::path(texture_path).filename();
  const std::string build_asset_path = absl::StrCat(config_->paths.assets(), "/", filename);
  const std::string source_asset_path = absl::StrCat(config_->paths.top(), "/assets/", filename);

  // Create rollback state tracking
  struct RollbackState {
    bool copied_to_build = false;
    bool copied_to_source = false;
    bool texture_inserted = false;
  } rollback;

  auto cleanup = absl::MakeCleanup([&]() {
    if (rollback.texture_inserted) {
      absl::Status result = db_->DeleteTexture(build_asset_path);
    }

    if (rollback.copied_to_source) {
      std::filesystem::remove(build_asset_path);
    }

    if (rollback.copied_to_build) {
      std::filesystem::remove(source_asset_path);
    }
  });

  // Step 1: Copy file to build directory
  try {
    std::filesystem::create_directories(std::filesystem::path(build_asset_path).parent_path());
    std::filesystem::copy_file(texture_path, build_asset_path);
    rollback.copied_to_build = true;
  } catch (const std::filesystem::filesystem_error& e) {
    return absl::InternalError("Failed to copy file to build directory: " + std::string(e.what()));
  }

  // Step 2: Copy file to source directory
  try {
    std::filesystem::create_directories(std::filesystem::path(source_asset_path).parent_path());
    std::filesystem::copy_file(texture_path, source_asset_path);
    rollback.copied_to_source = true;
  } catch (const std::filesystem::filesystem_error& e) {
    return absl::InternalError("Failed to copy file to source directory: " + std::string(e.what()));
  }

  // Step 3: Insert texture into database
  absl::StatusOr<uint16_t> result = db_->InsertTexture(build_asset_path);
  if (!result.ok()) {
    return result.status();
  }
  rollback.texture_inserted = true;

  // Success - don't cleanup
  std::move(cleanup).Cancel();
  LOG(INFO) << "Successfully inserted texture with path: " << build_asset_path;

  LOG(INFO) << __func__ << ", 7";
  return build_asset_path;
}

absl::Status Api::DeleteTexture(const std::string& texture_path) {
  // Validate input
  if (texture_path.empty()) {
    return absl::InvalidArgumentError("Texture path cannot be empty");
  }

  // Check if texture is still in use
  ASSIGN_OR_RETURN(int num_sprites, db_->NumSpritesWithTexture(texture_path));
  if (num_sprites > 0) {
    return absl::FailedPreconditionError(
        absl::StrCat("Texture is still in use by ", num_sprites, " sprites."));
  }

  // Step 1: Delete texture from database
  absl::Status db_status = db_->DeleteTexture(texture_path);
  if (!db_status.ok()) {
    LOG(ERROR) << "Failed to delete texture from database: " << db_status.message();
  }

  // Generate destination paths
  const std::string filename = std::filesystem::path(texture_path).filename();
  const std::string build_asset_path = absl::StrCat(config_->paths.assets(), "/", filename);
  const std::string source_asset_path = absl::StrCat(config_->paths.top(), "/assets/", filename);

  // Step 2: Delete build file
  if (!std::filesystem::remove(build_asset_path)) {
    LOG(ERROR) << "Failed to delete texture file from build directory: " << texture_path;
  }

  // Step 3: Delete source file
  if (!std::filesystem::remove(source_asset_path)) {
    LOG(ERROR) << "Failed to delete texture file from source directory: " << texture_path;
  }

  // Done
  LOG(INFO) << "Successfully deleted texture: " << texture_path;
  return absl::OkStatus();
}

absl::StatusOr<std::vector<Texture>> Api::GetAllTextures() { return db_->GetAllTextures(); }

absl::StatusOr<uint16_t> Api::UpsertSprite(const SpriteConfig& sprite_config) {
  // Step 1: Insert sprite into database
  absl::StatusOr<uint16_t> sprite_id = db_->InsertSprite(sprite_config);
  if (!sprite_id.ok()) {
    return sprite_id.status();
  }

  // Step 2: Insert sprite frames into database
  for (const auto& sprite_frame : sprite_config.sprite_frames) {
    absl::StatusOr<uint16_t> sprite_frame_id = db_->InsertSpriteFrame(*sprite_id, sprite_frame);
    if (!sprite_frame_id.ok()) {
      return sprite_frame_id.status();
    }
  }

  LOG(INFO) << "Successfully upserted sprite with ID: " << *sprite_id;
  return *sprite_id;
}

absl::Status Api::UpdateSprite(const SpriteConfig& sprite_config) {
  // Step 1: Update sprite in database
  absl::Status status = db_->UpdateSprite(sprite_config);
  if (!status.ok()) {
    return status;
  }

  // Step 2: Update/Insert sprite frames
  for (const auto& sprite_frame : sprite_config.sprite_frames) {
    if (sprite_frame.id > 0) {
      absl::Status sub_status = db_->UpdateSpriteFrame(sprite_frame);
      if (!sub_status.ok()) {
        LOG(ERROR) << "Failed to update sprite frame " << sprite_frame.id << ": " << sub_status;
        return sub_status;
      }
    } else {
      absl::StatusOr<uint16_t> sprite_frame_id =
          db_->InsertSpriteFrame(sprite_config.id, sprite_frame);
      if (!sprite_frame_id.ok()) {
        LOG(ERROR) << "Failed to insert new sprite frame: " << sprite_frame_id.status();
        return sprite_frame_id.status();
      }
    }
  }

  LOG(INFO) << "Successfully updated sprite with ID: " << sprite_config.id;
  return absl::OkStatus();
}

absl::Status Api::DeleteSprite(uint16_t sprite_id) {
  // Step 1: Delete sprite from database
  // Note: DbInterface::DeleteSprite should cascade delete sprite frames
  absl::Status db_status = db_->DeleteSprite(sprite_id);
  if (!db_status.ok()) {
    return db_status;
  }

  LOG(INFO) << "Successfully deleted sprite with ID: " << sprite_id;
  return absl::OkStatus();
}

absl::StatusOr<std::vector<SpriteConfig>> Api::GetAllSprites() {
  LOG(ERROR) << "Getting all sprites";
  return db_->GetAllSprites();
}

absl::StatusOr<std::vector<SpriteFrame>> Api::GetSpriteFrames(uint16_t sprite_id) {
  return db_->GetSpriteFrames(sprite_id);
}

absl::StatusOr<uint16_t> Api::InsertSpriteFrame(uint16_t sprite_id,
                                                const SpriteFrame& sprite_frame) {
  return db_->InsertSpriteFrame(sprite_id, sprite_frame);
}

absl::Status Api::DeleteSpriteFrame(uint16_t sprite_frame_id) {
  return db_->DeleteSpriteFrame(sprite_frame_id);
}

absl::Status Api::UpdateSpriteFrame(const SpriteFrame& sprite_frame) {
  return db_->UpdateSpriteFrame(sprite_frame);
}

}  // namespace zebes