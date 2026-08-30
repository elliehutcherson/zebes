#include "artwork/source_artwork.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& json, const char* key) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat("source artwork is missing '", key, "'"));
  }
  try {
    return json.at(key).get<T>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("source artwork field '", key, "' is invalid: ", error.what()));
  }
}

absl::StatusOr<std::optional<std::string>> RequiredNullableString(const nlohmann::json& json,
                                                                  const char* key) {
  if (!json.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat("source artwork is missing '", key, "'"));
  }
  if (json.at(key).is_null()) return std::nullopt;
  ASSIGN_OR_RETURN(std::string value, Required<std::string>(json, key));
  return value;
}

absl::Status ValidateTimestamp(std::string_view timestamp, std::string_view field) {
  if (timestamp.size() != 20 || timestamp[4] != '-' || timestamp[7] != '-' ||
      timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
      timestamp[19] != 'Z') {
    return absl::InvalidArgumentError(
        absl::StrCat(field, " must use UTC YYYY-MM-DDTHH:MM:SSZ form"));
  }
  constexpr std::array<size_t, 14> kDigitIndices = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
  for (const size_t index : kDigitIndices) {
    if (!std::isdigit(static_cast<unsigned char>(timestamp[index]))) {
      return absl::InvalidArgumentError(
          absl::StrCat(field, " must use UTC YYYY-MM-DDTHH:MM:SSZ form"));
    }
  }
  return absl::OkStatus();
}

nlohmann::json ProvenanceToJson(const SourceArtworkProvenance& provenance) {
  if (const auto* imported = std::get_if<ImportedArtworkProvenance>(&provenance)) {
    return {
        {"kind", "imported"},
        {"original_filename", imported->original_filename},
        {"imported_at_utc", imported->imported_at_utc},
    };
  }
  const auto& generated = std::get<GeneratedArtworkProvenance>(provenance);
  return {
      {"kind", "generated"},
      {"provider", generated.provider},
      {"model", generated.model},
      {"submitted_prompt", generated.submitted_prompt},
      {"revised_prompt", generated.revised_prompt.has_value()
                             ? nlohmann::json(*generated.revised_prompt)
                             : nlohmann::json(nullptr)},
      {"provider_request_id", generated.provider_request_id.has_value()
                                  ? nlohmann::json(*generated.provider_request_id)
                                  : nlohmann::json(nullptr)},
      {"generated_at_utc", generated.generated_at_utc},
  };
}

absl::StatusOr<SourceArtworkProvenance> ProvenanceFromJson(const nlohmann::json& json) {
  ASSIGN_OR_RETURN(const std::string kind, Required<std::string>(json, "kind"));
  if (kind == "imported") {
    ImportedArtworkProvenance imported;
    ASSIGN_OR_RETURN(imported.original_filename, Required<std::string>(json, "original_filename"));
    ASSIGN_OR_RETURN(imported.imported_at_utc, Required<std::string>(json, "imported_at_utc"));
    return SourceArtworkProvenance(std::move(imported));
  }
  if (kind == "generated") {
    GeneratedArtworkProvenance generated;
    ASSIGN_OR_RETURN(generated.provider, Required<std::string>(json, "provider"));
    ASSIGN_OR_RETURN(generated.model, Required<std::string>(json, "model"));
    ASSIGN_OR_RETURN(generated.submitted_prompt, Required<std::string>(json, "submitted_prompt"));
    ASSIGN_OR_RETURN(generated.revised_prompt, RequiredNullableString(json, "revised_prompt"));
    ASSIGN_OR_RETURN(generated.provider_request_id,
                     RequiredNullableString(json, "provider_request_id"));
    ASSIGN_OR_RETURN(generated.generated_at_utc, Required<std::string>(json, "generated_at_utc"));
    return SourceArtworkProvenance(std::move(generated));
  }
  return absl::InvalidArgumentError(
      absl::StrCat("unknown source artwork provenance kind '", kind, "'"));
}

}  // namespace

absl::Status ValidateSourceArtworkLimits(const SourceArtworkLimits& limits) {
  if (limits.maximum_width <= 0) {
    return absl::InvalidArgumentError("source artwork maximum width must be positive");
  }
  if (limits.maximum_height <= 0) {
    return absl::InvalidArgumentError("source artwork maximum height must be positive");
  }
  if (limits.maximum_pixels == 0) {
    return absl::InvalidArgumentError("source artwork maximum pixels must be positive");
  }
  if (limits.maximum_bytes < 4) {
    return absl::InvalidArgumentError(
        "source artwork maximum bytes must hold at least one RGBA8 pixel");
  }
  if (limits.maximum_encoded_bytes == 0) {
    return absl::InvalidArgumentError("source artwork maximum encoded bytes must be positive");
  }
  return absl::OkStatus();
}

absl::Status ValidateSourceArtworkPixels(const RgbaImage& image,
                                         const SourceArtworkLimits& limits) {
  if (!image.IsValid()) return absl::InvalidArgumentError("source artwork is not valid RGBA8");
  RETURN_IF_ERROR(ValidateSourceArtworkLimits(limits));

  const uint64_t pixels = static_cast<uint64_t>(image.width) * image.height;
  if (image.width > limits.maximum_width || image.height > limits.maximum_height ||
      pixels > limits.maximum_pixels || image.pixels.size() > limits.maximum_bytes) {
    return absl::ResourceExhaustedError(absl::StrCat("source artwork ", image.width, "x",
                                                     image.height, " (", image.pixels.size(),
                                                     " bytes) exceeds configured limits"));
  }
  return absl::OkStatus();
}

absl::Status ValidateSourceArtwork(const SourceArtwork& artwork) {
  if (artwork.id.empty() || artwork.name.empty() || artwork.source_path.empty()) {
    return absl::InvalidArgumentError("source artwork needs an ID, name, and source path");
  }
  if (artwork.width <= 0 || artwork.height <= 0) {
    return absl::InvalidArgumentError("source artwork dimensions must be positive");
  }
  if (!IsLowercaseSha256Digest(artwork.content_digest)) {
    return absl::InvalidArgumentError("source artwork content digest is not lowercase SHA-256");
  }
  if (const auto* imported = std::get_if<ImportedArtworkProvenance>(&artwork.provenance)) {
    if (imported->original_filename.empty()) {
      return absl::InvalidArgumentError("imported source artwork needs its original filename");
    }
    return ValidateTimestamp(imported->imported_at_utc, "source artwork import time");
  }
  const auto& generated = std::get<GeneratedArtworkProvenance>(artwork.provenance);
  if (generated.provider.empty() || generated.model.empty() || generated.submitted_prompt.empty()) {
    return absl::InvalidArgumentError(
        "generated source artwork needs provider, model, and submitted prompt");
  }
  return ValidateTimestamp(generated.generated_at_utc, "source artwork generation time");
}

nlohmann::json SourceArtworkToJson(const SourceArtwork& artwork) {
  return {
      {"schema_version", kSourceArtworkSchemaVersion},
      {"id", artwork.id},
      {"name", artwork.name},
      {"source_path", artwork.source_path},
      {"provenance", ProvenanceToJson(artwork.provenance)},
      {"width", artwork.width},
      {"height", artwork.height},
      {"content_digest", artwork.content_digest},
  };
}

absl::StatusOr<SourceArtwork> SourceArtworkFromJson(const nlohmann::json& json) {
  ASSIGN_OR_RETURN(const int schema_version, Required<int>(json, "schema_version"));
  if (schema_version != kSourceArtworkSchemaVersion) {
    return absl::FailedPreconditionError(absl::StrCat(
        "source artwork schema version ", schema_version, " is not version ",
        kSourceArtworkSchemaVersion, "; run scripts/migrate_definitions.py to bring it forward"));
  }
  SourceArtwork artwork;
  ASSIGN_OR_RETURN(artwork.id, Required<std::string>(json, "id"));
  ASSIGN_OR_RETURN(artwork.name, Required<std::string>(json, "name"));
  ASSIGN_OR_RETURN(artwork.source_path, Required<std::string>(json, "source_path"));
  ASSIGN_OR_RETURN(const nlohmann::json provenance, Required<nlohmann::json>(json, "provenance"));
  ASSIGN_OR_RETURN(artwork.provenance, ProvenanceFromJson(provenance));
  ASSIGN_OR_RETURN(artwork.width, Required<int>(json, "width"));
  ASSIGN_OR_RETURN(artwork.height, Required<int>(json, "height"));
  ASSIGN_OR_RETURN(artwork.content_digest, Required<std::string>(json, "content_digest"));
  RETURN_IF_ERROR(ValidateSourceArtwork(artwork));
  return artwork;
}

}  // namespace zebes
