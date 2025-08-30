#include "api/api.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
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
  if (options.sprite_manager == nullptr) {
    return absl::InvalidArgumentError("SpriteManager is null.");
  }
  return std::unique_ptr<Api>(new Api(options));
}

Api::Api(const Options& options)
    : config_(options.config),
      db_(options.db),
      sprite_manager_(options.sprite_manager) {}

absl::StatusOr<std::string> Api::CreateTexture(
    const std::string& texture_path) {
  // Validate input
  if (!std::filesystem::exists(texture_path)) {
    return absl::NotFoundError(
        absl::StrCat("Source texture file does not exist: ", texture_path));
  }

  // Generate destination paths
  const std::string filename = std::filesystem::path(texture_path).filename();
  const std::string build_asset_path =
      absl::StrCat(config_->paths.assets(), "/", filename);
  const std::string source_asset_path =
      absl::StrCat(config_->paths.top(), "/assets/", filename);

  // Create rollback state tracking
  struct RollbackState {
    bool copied_to_build = false;
    bool copied_to_source = false;
    bool texture_inserted = false;
    bool sprite_manager_updated = false;
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
    std::filesystem::create_directories(
        std::filesystem::path(build_asset_path).parent_path());
    std::filesystem::copy_file(texture_path, build_asset_path);
    rollback.copied_to_build = true;
  } catch (const std::filesystem::filesystem_error& e) {
    return absl::InternalError("Failed to copy file to build directory: " +
                               std::string(e.what()));
  }

  // Step 2: Copy file to source directory
  try {
    std::filesystem::create_directories(
        std::filesystem::path(source_asset_path).parent_path());
    std::filesystem::copy_file(texture_path, source_asset_path);
    rollback.copied_to_source = true;
  } catch (const std::filesystem::filesystem_error& e) {
    return absl::InternalError("Failed to copy file to source directory: " +
                               std::string(e.what()));
  }

  // Step 3: Insert texture into database
  absl::StatusOr<uint16_t> result = db_->InsertTexture(build_asset_path);
  if (!result.ok()) {
    return result.status();
  }
  rollback.texture_inserted = true;

  // Step 4: Add texture to sprite manager
  absl::Status sprite_manager_status =
      sprite_manager_->AddTexture(build_asset_path);
  if (!sprite_manager_status.ok()) {
    return sprite_manager_status;
  }
  rollback.sprite_manager_updated = true;

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

  // Step 1: Remove texture from sprite manager
  absl::Status sprite_manager_status =
      sprite_manager_->DeleteTexture(texture_path);
  if (!sprite_manager_status.ok()) {
    LOG(ERROR) << "Failed to remove texture from sprite manager: "
               << sprite_manager_status.message();
  }

  // Step 2: Delete texture from database
  absl::Status db_status = db_->DeleteTexture(texture_path);
  if (!db_status.ok()) {
    LOG(ERROR) << "Failed to delete texture from database: "
               << db_status.message();
  }

  // Generate destination paths
  const std::string filename = std::filesystem::path(texture_path).filename();
  const std::string build_asset_path =
      absl::StrCat(config_->paths.assets(), "/", filename);
  const std::string source_asset_path =
      absl::StrCat(config_->paths.top(), "/assets/", filename);

  // Step 3: Delete build file
  if (!std::filesystem::remove(build_asset_path)) {
    LOG(ERROR) << "Failed to delete texture file from build directory: "
               << texture_path;
  }

  // Step 4: Delete source file
  if (!std::filesystem::remove(source_asset_path)) {
    LOG(ERROR) << "Failed to delete texture file from source directory: "
               << texture_path;
  }

  // Done
  LOG(INFO) << "Successfully deleted texture: " << texture_path;
  return absl::OkStatus();
}

}  // namespace zebes