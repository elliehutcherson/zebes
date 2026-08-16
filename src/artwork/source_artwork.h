#pragma once

#include <optional>
#include <string>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
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

// Editor-only retained input. Runtime texture stores never load this image;
// its decoded pixels are the reproducible authority for prop regeneration.
struct SourceArtwork {
  std::string id;
  std::string name;
  std::string source_path;
  SourceArtworkProvenance provenance;
  int width = 0;
  int height = 0;
  std::string content_digest;
};

inline constexpr int kSourceArtworkSchemaVersion = 1;

absl::Status ValidateSourceArtwork(const SourceArtwork& artwork);
nlohmann::json SourceArtworkToJson(const SourceArtwork& artwork);
absl::StatusOr<SourceArtwork> SourceArtworkFromJson(const nlohmann::json& json);

}  // namespace zebes
