#include "resources/source_artwork_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/prop_artwork_pipeline.h"
#include "common/image_digest.h"
#include "common/resource_identity.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/source_artworks";
constexpr char kImagesPath[] = "source_art/props";

absl::StatusOr<SourceArtwork> LoadDefinition(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));
  try {
    nlohmann::json json;
    stream >> json;
    return SourceArtworkFromJson(json);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid source artwork JSON in ", path, ": ", error.what()));
  }
}

absl::Status WriteDefinition(const std::string& path, const SourceArtwork& artwork) {
  const std::string temporary = absl::StrCat(path, ".tmp");
  std::ofstream stream(temporary, std::ios::trunc);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not write source artwork: ", temporary));
  }
  stream << SourceArtworkToJson(artwork).dump(2);
  stream.flush();
  if (!stream.good()) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return absl::InternalError(absl::StrCat("failed while writing source artwork: ", temporary));
  }
  stream.close();
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return absl::InternalError(absl::StrCat("could not commit source artwork: ", error.message()));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<SourceArtworkManager>> SourceArtworkManager::Create(
    std::string root_path, PropSourceLimits limits) {
  if (root_path.empty()) return absl::InvalidArgumentError("source artwork asset root is empty");
  RgbaImage smallest{.width = 1, .height = 1, .pixels = {0, 0, 0, 0}};
  RETURN_IF_ERROR(ValidatePropSource(smallest, limits));
  return std::unique_ptr<SourceArtworkManager>(
      new SourceArtworkManager(std::move(root_path), limits));
}

SourceArtworkManager::SourceArtworkManager(std::string root_path, PropSourceLimits limits)
    : root_path_(std::move(root_path)),
      definitions_path_(absl::StrCat(root_path_, "/", kDefinitionsPath)),
      images_path_(absl::StrCat(root_path_, "/", kImagesPath)),
      limits_(limits) {}

absl::Status SourceArtworkManager::EnsureDirectories() const {
  std::error_code error;
  std::filesystem::create_directories(definitions_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create source artwork definitions: ", error.message()));
  }
  std::filesystem::create_directories(images_path_, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create source artwork image directory: ", error.message()));
  }
  return absl::OkStatus();
}

std::string SourceArtworkManager::DefinitionPath(const std::string& id) const {
  return absl::StrCat(definitions_path_, "/", id, ".json");
}

std::string SourceArtworkManager::ImagePath(const std::string& id) const {
  return absl::StrCat(images_path_, "/", id, ".png");
}

std::string SourceArtworkManager::RelativeImagePath(const std::string& id) {
  return absl::StrCat(kImagesPath, "/", id, ".png");
}

absl::Status SourceArtworkManager::ValidateStoredArtwork(const SourceArtwork& artwork) const {
  RETURN_IF_ERROR(ValidateSourceArtwork(artwork));
  if (!IsPathSafeResourceId(artwork.id)) {
    return absl::InvalidArgumentError("source artwork ID is not path-safe");
  }
  if (artwork.source_path != RelativeImagePath(artwork.id)) {
    return absl::InvalidArgumentError(
        "source artwork path must be the ID-backed path under source_art/props");
  }
  ASSIGN_OR_RETURN(const RgbaImage image, ReadPng(ImagePath(artwork.id)));
  RETURN_IF_ERROR(ValidatePropSource(image, limits_));
  if (image.width != artwork.width || image.height != artwork.height) {
    return absl::DataLossError("source artwork dimensions do not match its retained image");
  }
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
  if (digest != artwork.content_digest) {
    return absl::DataLossError("source artwork digest does not match its retained image");
  }
  return absl::OkStatus();
}

absl::Status SourceArtworkManager::LoadAllArtwork() {
  RETURN_IF_ERROR(EnsureDirectories());
  absl::flat_hash_map<std::string, std::unique_ptr<SourceArtwork>> loaded;
  ResourceLoadFailures failures;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(definitions_path_)) {
    if (entry.path().extension() != ".json") continue;
    absl::StatusOr<SourceArtwork> parsed = LoadDefinition(entry.path().string());
    if (!parsed.ok()) {
      failures.Add(entry.path().string(), parsed.status());
      continue;
    }
    SourceArtwork artwork = std::move(*parsed);
    absl::Status valid = ValidateStoredArtwork(artwork);
    if (!valid.ok()) {
      failures.Add(entry.path().string(), valid);
      continue;
    }
    if (entry.path().stem() != artwork.id) {
      failures.Add(entry.path().string(),
                   absl::InvalidArgumentError("source artwork filename does not match its ID"));
      continue;
    }
    if (loaded.contains(artwork.id)) {
      failures.Add(entry.path().string(), absl::AlreadyExistsError(absl::StrCat(
                                              "duplicate source artwork ID ", artwork.id)));
      continue;
    }
    const std::string id = artwork.id;
    loaded[id] = std::make_unique<SourceArtwork>(std::move(artwork));
  }
  RETURN_IF_ERROR(failures.ToStatus("source artwork"));
  artwork_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> SourceArtworkManager::CreateArtwork(std::string name,
                                                                SourceArtworkProvenance provenance,
                                                                const RgbaImage& image) {
  RETURN_IF_ERROR(EnsureDirectories());
  RETURN_IF_ERROR(ValidatePropSource(image, limits_));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));

  const std::string id = GenerateGuid();
  SourceArtwork artwork{
      .id = id,
      .name = std::move(name),
      .source_path = RelativeImagePath(id),
      .provenance = std::move(provenance),
      .width = image.width,
      .height = image.height,
      .content_digest = digest,
  };
  RETURN_IF_ERROR(ValidateSourceArtwork(artwork));

  const std::string image_path = ImagePath(id);
  const std::string definition_path = DefinitionPath(id);
  if (std::filesystem::exists(image_path) || std::filesystem::exists(definition_path)) {
    return absl::AlreadyExistsError("generated source artwork ID already exists");
  }

  const std::string temporary_image = absl::StrCat(image_path, ".tmp");
  RETURN_IF_ERROR(WritePng(temporary_image, image.width, image.height, image.pixels));
  std::error_code error;
  std::filesystem::rename(temporary_image, image_path, error);
  if (error) {
    std::filesystem::remove(temporary_image, error);
    return absl::InternalError(
        absl::StrCat("could not commit source artwork image: ", error.message()));
  }

  absl::Status definition_status = WriteDefinition(definition_path, artwork);
  if (!definition_status.ok()) {
    std::filesystem::remove(image_path, error);
    return definition_status;
  }
  artwork_[id] = std::make_unique<SourceArtwork>(std::move(artwork));
  return id;
}

absl::StatusOr<SourceArtwork*> SourceArtworkManager::GetArtwork(const std::string& id) {
  auto found = artwork_.find(id);
  if (found == artwork_.end()) {
    return absl::NotFoundError(absl::StrCat("source artwork ", id, " is not loaded"));
  }
  return found->second.get();
}

std::vector<SourceArtwork> SourceArtworkManager::GetAllArtwork() const {
  std::vector<SourceArtwork> result;
  result.reserve(artwork_.size());
  for (const auto& [id, artwork] : artwork_) result.push_back(*artwork);
  std::sort(result.begin(), result.end(),
            [](const SourceArtwork& left, const SourceArtwork& right) {
              if (left.name != right.name) return left.name < right.name;
              return left.id < right.id;
            });
  return result;
}

absl::StatusOr<RgbaImage> SourceArtworkManager::ReadArtworkPixels(const std::string& id) const {
  const auto found = artwork_.find(id);
  if (found == artwork_.end()) {
    return absl::NotFoundError(absl::StrCat("source artwork ", id, " is not loaded"));
  }
  ASSIGN_OR_RETURN(RgbaImage image, ReadPng(ImagePath(id)));
  RETURN_IF_ERROR(ValidatePropSource(image, limits_));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
  if (digest != found->second->content_digest) {
    return absl::DataLossError("source artwork changed since it was loaded");
  }
  return image;
}

absl::Status SourceArtworkManager::DeleteArtwork(const std::string& id) {
  auto found = artwork_.find(id);
  if (found == artwork_.end()) return absl::NotFoundError("source artwork is not loaded");

  const std::string definition = DefinitionPath(id);
  const std::string image = ImagePath(id);
  const std::string definition_tombstone = absl::StrCat(definition, ".deleting");
  const std::string image_tombstone = absl::StrCat(image, ".deleting");
  std::error_code error;
  std::filesystem::rename(definition, definition_tombstone, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not stage source artwork definition deletion: ", error.message()));
  }
  std::filesystem::rename(image, image_tombstone, error);
  if (error) {
    const std::string image_error = error.message();
    std::error_code rollback_error;
    std::filesystem::rename(definition_tombstone, definition, rollback_error);
    if (rollback_error) {
      return absl::InternalError(
          absl::StrCat("could not stage source artwork image deletion: ", image_error,
                       "; restoring its definition also failed: ", rollback_error.message()));
    }
    return absl::InternalError(
        absl::StrCat("could not stage source artwork image deletion: ", image_error));
  }
  artwork_.erase(found);

  std::error_code definition_error;
  std::filesystem::remove(definition_tombstone, definition_error);
  std::error_code image_error;
  std::filesystem::remove(image_tombstone, image_error);
  if (definition_error || image_error) {
    return absl::InternalError(absl::StrCat(
        "source artwork was deleted but tombstone cleanup failed: ", definition_error.message(),
        definition_error && image_error ? "; " : "", image_error.message()));
  }
  return absl::OkStatus();
}

}  // namespace zebes
