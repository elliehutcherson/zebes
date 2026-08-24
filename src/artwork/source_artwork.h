#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "nlohmann/json_fwd.hpp"

namespace zebes {

struct ImportedArtworkProvenance {
  std::string original_filename;
  std::string imported_at_utc;
};

struct GeneratedArtworkProvenance {
  std::string provider;
  std::string model;
  std::string submitted_prompt;
  std::optional<std::string> revised_prompt;
  std::optional<std::string> provider_request_id;
  std::string generated_at_utc;
};

using SourceArtworkProvenance = std::variant<ImportedArtworkProvenance, GeneratedArtworkProvenance>;

struct SourceArtworkLimits {
  int maximum_width = 4096;
  int maximum_height = 4096;
  size_t maximum_pixels = 16 * 1024 * 1024;
  size_t maximum_bytes = 64 * 1024 * 1024;
};

// Editor-only retained input. Runtime texture stores never load this image;
// its decoded pixels are the reproducible authority for artwork regeneration.
struct SourceArtwork {
  std::string id;
  std::string name;
  std::string source_path;
  SourceArtworkProvenance provenance;
  int width = 0;
  int height = 0;
  std::string content_digest;
};

inline constexpr int kSourceArtworkSchemaVersion = 2;

absl::Status ValidateSourceArtworkLimits(const SourceArtworkLimits& limits);
absl::Status ValidateSourceArtworkPixels(const RgbaImage& image, const SourceArtworkLimits& limits);
absl::Status ValidateSourceArtwork(const SourceArtwork& artwork);
nlohmann::json SourceArtworkToJson(const SourceArtwork& artwork);
absl::StatusOr<SourceArtwork> SourceArtworkFromJson(const nlohmann::json& json);

}  // namespace zebes
