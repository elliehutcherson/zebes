#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"

namespace zebes {

// Owns editor-only source definitions and their ID-backed lossless PNGs. It
// validates decoded pixels and canonical digest on every load; runtime texture
// resources are deliberately outside this boundary.
class SourceArtworkManager {
 public:
  static absl::StatusOr<std::unique_ptr<SourceArtworkManager>> Create(
      std::string root_path, SourceArtworkLimits limits = {});

  virtual ~SourceArtworkManager() = default;

  virtual absl::Status LoadAllArtwork();
  virtual absl::StatusOr<std::string> CreateArtwork(std::string name,
                                                    SourceArtworkProvenance provenance,
                                                    const RgbaImage& image);
  // Replaces retained pixels and provenance without changing the identity or
  // path referenced by generated-asset recipes. `expected` is an optimistic
  // concurrency snapshot; stale callers are refused before anything is
  // written. The operation either publishes both the PNG and definition or
  // restores the prior pair.
  virtual absl::Status ReplaceArtwork(const SourceArtwork& expected,
                                      const SourceArtwork& replacement,
                                      const RgbaImage& replacement_pixels);
  virtual absl::StatusOr<SourceArtwork*> GetArtwork(const std::string& id);
  virtual std::vector<SourceArtwork> GetAllArtwork() const;
  virtual absl::StatusOr<RgbaImage> ReadArtworkPixels(const std::string& id) const;
  virtual absl::Status DeleteArtwork(const std::string& id);

 protected:
  SourceArtworkManager() = default;

 private:
  SourceArtworkManager(std::string root_path, SourceArtworkLimits limits);

  absl::Status EnsureDirectories() const;
  absl::Status ValidateStoredArtwork(const SourceArtwork& artwork) const;
  std::string DefinitionPath(const std::string& id) const;
  std::string ImagePath(const std::string& id) const;
  static std::string RelativeImagePath(const std::string& id);

  std::string root_path_;
  std::string definitions_path_;
  std::string images_path_;
  SourceArtworkLimits limits_;
  absl::flat_hash_map<std::string, std::unique_ptr<SourceArtwork>> artwork_;
};

}  // namespace zebes
