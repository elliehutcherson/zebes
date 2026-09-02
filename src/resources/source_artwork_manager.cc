#include "resources/source_artwork_manager.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/named_asset_order.h"
#include "common/resource_identity.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "nlohmann/json.hpp"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr char kDefinitionsPath[] = "definitions/source_artworks";
constexpr char kImagesPath[] = "source_art";

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

absl::StatusOr<size_t> BoundedEncodedImageSize(const std::string& path, size_t maximum_bytes) {
  if (maximum_bytes == 0) {
    return absl::InvalidArgumentError("source artwork encoded byte limit must be positive");
  }

  std::error_code error;
  const uintmax_t file_bytes = std::filesystem::file_size(path, error);
  if (error) {
    return absl::NotFoundError(
        absl::StrCat("could not inspect source artwork image ", path, ": ", error.message()));
  }
  if (file_bytes == 0) {
    return absl::DataLossError(absl::StrCat("source artwork image is empty: ", path));
  }
  if (file_bytes > static_cast<uintmax_t>(maximum_bytes)) {
    return absl::ResourceExhaustedError(absl::StrCat("source artwork encoded image is ", file_bytes,
                                                     " bytes, exceeding the ", maximum_bytes,
                                                     " byte limit"));
  }
  if (!std::in_range<int>(file_bytes) || !std::in_range<size_t>(file_bytes) ||
      !std::in_range<std::streamsize>(file_bytes)) {
    return absl::OutOfRangeError("source artwork encoded image exceeds the reader's range");
  }
  return static_cast<size_t>(file_bytes);
}

absl::StatusOr<std::vector<uint8_t>> ReadBoundedEncodedImage(const std::string& path,
                                                             size_t maximum_bytes) {
  ASSIGN_OR_RETURN(const size_t file_bytes, BoundedEncodedImageSize(path, maximum_bytes));
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open source artwork image ", path));
  }
  std::vector<uint8_t> encoded(file_bytes);
  stream.read(reinterpret_cast<char*>(encoded.data()),
              static_cast<std::streamsize>(encoded.size()));
  if (static_cast<size_t>(stream.gcount()) != encoded.size()) {
    return absl::DataLossError(absl::StrCat("source artwork image changed while reading: ", path));
  }
  if (stream.peek() != std::char_traits<char>::eof()) {
    return absl::DataLossError(absl::StrCat("source artwork image changed while reading: ", path));
  }
  if (stream.bad()) {
    return absl::DataLossError(absl::StrCat("could not read source artwork image: ", path));
  }
  return encoded;
}

}  // namespace

absl::StatusOr<std::unique_ptr<SourceArtworkManager>> SourceArtworkManager::Create(
    std::string root_path, SourceArtworkLimits limits) {
  if (root_path.empty()) return absl::InvalidArgumentError("source artwork asset root is empty");
  RETURN_IF_ERROR(ValidateSourceArtworkLimits(limits));
  return std::unique_ptr<SourceArtworkManager>(
      new SourceArtworkManager(std::move(root_path), limits));
}

SourceArtworkManager::SourceArtworkManager(std::string root_path, SourceArtworkLimits limits)
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
        "source artwork path must be the ID-backed path under source_art");
  }
  return ReadStoredArtworkPixels(artwork, MaximumDecodedPixels()).status();
}

size_t SourceArtworkManager::MaximumDecodedPixels() const {
  return std::min(limits_.maximum_pixels, limits_.maximum_bytes / 4);
}

absl::Status SourceArtworkManager::ValidateReadPixelLimit(size_t maximum_pixels) const {
  const size_t manager_maximum_pixels = MaximumDecodedPixels();
  if (maximum_pixels == 0) {
    return absl::InvalidArgumentError("source artwork read pixel limit must be positive");
  }
  if (maximum_pixels > manager_maximum_pixels) {
    return absl::InvalidArgumentError(absl::StrCat("source artwork read pixel limit ",
                                                   maximum_pixels, " exceeds the manager maximum ",
                                                   manager_maximum_pixels));
  }
  if (!std::in_range<int64_t>(maximum_pixels)) {
    return absl::OutOfRangeError("source artwork read pixel limit exceeds the decoder's range");
  }
  return absl::OkStatus();
}

absl::StatusOr<RgbaImage> SourceArtworkManager::ReadStoredArtworkPixels(
    const SourceArtwork& artwork, size_t maximum_pixels) const {
  RETURN_IF_ERROR(ValidateReadPixelLimit(maximum_pixels));

  ASSIGN_OR_RETURN(const std::vector<uint8_t> encoded,
                   ReadBoundedEncodedImage(ImagePath(artwork.id), limits_.maximum_encoded_bytes));
  ASSIGN_OR_RETURN(RgbaImage image, DecodeImage(encoded, static_cast<int64_t>(maximum_pixels)));
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(image, limits_));
  if (image.width != artwork.width || image.height != artwork.height) {
    return absl::DataLossError("source artwork dimensions do not match its retained image");
  }
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
  if (digest != artwork.content_digest) {
    return absl::DataLossError("source artwork digest does not match its retained image");
  }
  return image;
}

absl::Status SourceArtworkManager::LoadAllArtwork() {
  RETURN_IF_ERROR(EnsureDirectories());
  absl::flat_hash_map<std::string, std::unique_ptr<SourceArtwork>> loaded;
  RETURN_IF_ERROR(LoadJsonDefinitions(
      definitions_path_, "source artwork",
      [this, &loaded](const std::filesystem::path& path) -> absl::Status {
        ASSIGN_OR_RETURN(SourceArtwork artwork, LoadDefinition(path.string()));
        RETURN_IF_ERROR(ValidateStoredArtwork(artwork));
        if (path.stem() != artwork.id) {
          return absl::InvalidArgumentError("source artwork filename does not match its ID");
        }
        if (loaded.contains(artwork.id)) {
          return absl::AlreadyExistsError(absl::StrCat("duplicate source artwork ID ", artwork.id));
        }
        const std::string id = artwork.id;
        loaded[id] = std::make_unique<SourceArtwork>(std::move(artwork));
        return absl::OkStatus();
      }));
  artwork_ = std::move(loaded);
  return absl::OkStatus();
}

absl::StatusOr<std::string> SourceArtworkManager::CreateArtwork(std::string name,
                                                                SourceArtworkProvenance provenance,
                                                                const RgbaImage& image) {
  RETURN_IF_ERROR(EnsureDirectories());
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(image, limits_));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
  const std::string id = GenerateGuid();
  const SourceArtwork artwork{
      .id = id,
      .name = std::move(name),
      .source_path = RelativeImagePath(id),
      .provenance = std::move(provenance),
      .width = image.width,
      .height = image.height,
      .content_digest = digest,
  };
  RETURN_IF_ERROR(PreflightArtworkWithId(artwork, image, digest));
  RETURN_IF_ERROR(CommitArtworkWithId(artwork, image));
  return id;
}

absl::Status SourceArtworkManager::PreflightArtworkWithId(const SourceArtwork& artwork,
                                                          const RgbaImage& image) const {
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(image, limits_));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
  return PreflightArtworkWithId(artwork, image, digest);
}

absl::Status SourceArtworkManager::PreflightArtworkWithId(const SourceArtwork& artwork,
                                                          const RgbaImage& image,
                                                          std::string_view digest) const {
  RETURN_IF_ERROR(ValidateSourceArtwork(artwork));
  if (!IsPathSafeResourceId(artwork.id) || artwork.source_path != RelativeImagePath(artwork.id)) {
    return absl::InvalidArgumentError(
        "restored source artwork must keep its ID-backed source_art path");
  }
  if (artwork.width != image.width || artwork.height != image.height) {
    return absl::InvalidArgumentError("restored source artwork dimensions do not match its pixels");
  }
  if (artwork.content_digest != digest) {
    return absl::InvalidArgumentError("restored source artwork digest does not match its pixels");
  }
  if (artwork_.contains(artwork.id) || std::filesystem::exists(ImagePath(artwork.id)) ||
      std::filesystem::exists(DefinitionPath(artwork.id))) {
    return absl::AlreadyExistsError(absl::StrCat("source artwork ", artwork.id, " already exists"));
  }
  return absl::OkStatus();
}

absl::Status SourceArtworkManager::CreateArtworkWithId(const SourceArtwork& artwork,
                                                       const RgbaImage& image) {
  RETURN_IF_ERROR(EnsureDirectories());
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(image, limits_));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
  RETURN_IF_ERROR(PreflightArtworkWithId(artwork, image, digest));
  return CommitArtworkWithId(artwork, image);
}

absl::Status SourceArtworkManager::CommitArtworkWithId(const SourceArtwork& artwork,
                                                       const RgbaImage& image) {
  const std::string image_path = ImagePath(artwork.id);
  const std::string definition_path = DefinitionPath(artwork.id);
  const std::string temporary_image = absl::StrCat(image_path, ".tmp");
  RETURN_IF_ERROR(WritePng(temporary_image, image.width, image.height, image.pixels));
  absl::Cleanup remove_temporary = [&temporary_image] {
    std::error_code ignored;
    std::filesystem::remove(temporary_image, ignored);
  };
  RETURN_IF_ERROR(BoundedEncodedImageSize(temporary_image, limits_.maximum_encoded_bytes).status());
  std::error_code error;
  std::filesystem::rename(temporary_image, image_path, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not commit source artwork image: ", error.message()));
  }
  std::move(remove_temporary).Cancel();
  absl::Cleanup remove_image = [&image_path] {
    std::error_code ignored;
    std::filesystem::remove(image_path, ignored);
  };

  RETURN_IF_ERROR(WriteTextFileAtomically(definition_path, SourceArtworkToJson(artwork).dump(2)));
  artwork_[artwork.id] = std::make_unique<SourceArtwork>(artwork);
  std::move(remove_image).Cancel();
  return absl::OkStatus();
}

absl::Status SourceArtworkManager::ReplaceArtwork(const SourceArtwork& expected,
                                                  const SourceArtwork& replacement,
                                                  const RgbaImage& replacement_pixels) {
  RETURN_IF_ERROR(EnsureDirectories());
  auto found = artwork_.find(expected.id);
  if (found == artwork_.end()) {
    return absl::NotFoundError(absl::StrCat("source artwork ", expected.id, " is not loaded"));
  }
  if (SourceArtworkToJson(*found->second) != SourceArtworkToJson(expected)) {
    return absl::FailedPreconditionError("source artwork changed before it could be replaced");
  }
  RETURN_IF_ERROR(ReadArtworkPixels(expected.id).status());

  RETURN_IF_ERROR(ValidateSourceArtwork(replacement));
  if (!IsPathSafeResourceId(replacement.id) ||
      replacement.source_path != RelativeImagePath(replacement.id)) {
    return absl::InvalidArgumentError(
        "replacement source artwork must keep its ID-backed source_art path");
  }
  if (replacement.id != expected.id || replacement.name != expected.name ||
      replacement.source_path != expected.source_path) {
    return absl::InvalidArgumentError(
        "replacing source artwork cannot change its ID, name, or source path");
  }
  RETURN_IF_ERROR(ValidateSourceArtworkPixels(replacement_pixels, limits_));
  if (replacement.width != replacement_pixels.width ||
      replacement.height != replacement_pixels.height) {
    return absl::InvalidArgumentError(
        "replacement source artwork dimensions do not match its pixels");
  }
  ASSIGN_OR_RETURN(const std::string replacement_digest, RgbaImageDigest(replacement_pixels));
  if (replacement.content_digest != replacement_digest) {
    return absl::InvalidArgumentError(
        "replacement source artwork digest does not match its pixels");
  }

  const std::string image_path = ImagePath(expected.id);
  const std::string definition_path = DefinitionPath(expected.id);
  const std::string temporary_image = absl::StrCat(image_path, ".replacing");
  const std::string backup_image = absl::StrCat(image_path, ".replaced");
  if (std::filesystem::exists(temporary_image) || std::filesystem::exists(backup_image)) {
    return absl::FailedPreconditionError(
        "source artwork has an unfinished replacement; resolve it before retrying");
  }

  RETURN_IF_ERROR(WritePng(temporary_image, replacement_pixels.width, replacement_pixels.height,
                           replacement_pixels.pixels));
  absl::Cleanup remove_temporary = [&temporary_image] {
    std::error_code ignored;
    std::filesystem::remove(temporary_image, ignored);
  };
  RETURN_IF_ERROR(BoundedEncodedImageSize(temporary_image, limits_.maximum_encoded_bytes).status());

  std::error_code error;
  std::filesystem::rename(image_path, backup_image, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not stage retained source replacement: ", error.message()));
  }

  const auto restore_prior_image = [&image_path, &backup_image]() -> absl::Status {
    std::error_code remove_error;
    std::filesystem::remove(image_path, remove_error);
    if (remove_error) {
      return absl::InternalError(absl::StrCat("could not discard failed replacement source image: ",
                                              remove_error.message()));
    }
    std::error_code restore_error;
    std::filesystem::rename(backup_image, image_path, restore_error);
    if (restore_error) {
      return absl::InternalError(
          absl::StrCat("could not restore prior source image: ", restore_error.message()));
    }
    return absl::OkStatus();
  };

  std::filesystem::rename(temporary_image, image_path, error);
  if (error) {
    const std::string publish_error = error.message();
    const absl::Status restore_status = restore_prior_image();
    if (!restore_status.ok()) {
      return absl::InternalError(
          absl::StrCat("could not publish replacement source artwork image: ", publish_error, "; ",
                       restore_status.message()));
    }
    return absl::InternalError(
        absl::StrCat("could not publish replacement source artwork image: ", publish_error));
  }
  std::move(remove_temporary).Cancel();

  const absl::Status definition_status =
      WriteTextFileAtomically(definition_path, SourceArtworkToJson(replacement).dump(2));
  if (!definition_status.ok()) {
    const absl::Status restore_status = restore_prior_image();
    if (!restore_status.ok()) {
      return absl::Status(definition_status.code(), absl::StrCat(definition_status.message(), "; ",
                                                                 restore_status.message()));
    }
    return definition_status;
  }

  *found->second = replacement;
  std::filesystem::remove(backup_image, error);
  return absl::OkStatus();
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
  std::ranges::sort(result, NamedAssetLess{});
  return result;
}

absl::StatusOr<RgbaImage> SourceArtworkManager::ReadArtworkPixels(const std::string& id) const {
  return ReadArtworkPixels(id, MaximumDecodedPixels());
}

absl::StatusOr<RgbaImage> SourceArtworkManager::ReadArtworkPixels(const std::string& id,
                                                                  size_t maximum_pixels) const {
  RETURN_IF_ERROR(ValidateReadPixelLimit(maximum_pixels));
  const auto found = artwork_.find(id);
  if (found == artwork_.end()) {
    return absl::NotFoundError(absl::StrCat("source artwork ", id, " is not loaded"));
  }
  return ReadStoredArtworkPixels(*found->second, maximum_pixels);
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
