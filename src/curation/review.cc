#include "curation/review.h"

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

#include "absl/status/status.h"
#include "common/atomic_directory_publisher.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

constexpr int64_t kMaximumReviewPixels = 512LL * 1024 * 1024;

bool IsSafeArtifactPath(const std::string& relative_path) {
  const std::filesystem::path path(relative_path);
  if (path.empty() || path.is_absolute() || path.extension() != ".png") return false;
  for (const std::filesystem::path& component : path) {
    if (component == "." || component == "..") return false;
  }
  return path.lexically_normal() == path;
}

}  // namespace

std::string_view CurationFindingSeverityId(CurationFindingSeverity severity) {
  switch (severity) {
    case CurationFindingSeverity::kInfo:
      return "info";
    case CurationFindingSeverity::kWarning:
      return "warning";
  }
  return {};
}

absl::Status ValidateCurationReview(const CurationReview& review) {
  if (review.kind.empty() || review.asset_id.empty() || review.asset_name.empty()) {
    return absl::InvalidArgumentError("curation review needs a kind, asset ID, and asset name");
  }
  if (!review.metadata.is_object()) {
    return absl::InvalidArgumentError("curation review metadata must be an object");
  }
  if (review.artifacts.empty()) {
    return absl::InvalidArgumentError("curation review must contain an artifact");
  }

  std::set<std::string> ids;
  std::set<std::string> paths;
  int64_t total_pixels = 0;
  for (const CurationArtifact& artifact : review.artifacts) {
    if (artifact.id.empty() || artifact.description.empty() || !artifact.image.IsValid() ||
        !artifact.metadata.is_object() || !IsSafeArtifactPath(artifact.relative_path)) {
      return absl::InvalidArgumentError("curation review contains an invalid artifact");
    }
    if (!ids.insert(artifact.id).second || !paths.insert(artifact.relative_path).second) {
      return absl::InvalidArgumentError("curation artifact IDs and paths must be unique");
    }
    total_pixels += static_cast<int64_t>(artifact.image.width) * artifact.image.height;
    if (total_pixels > kMaximumReviewPixels) {
      return absl::ResourceExhaustedError("curation review exceeds its total pixel limit");
    }
  }

  for (const CurationFinding& finding : review.findings) {
    if (CurationFindingSeverityId(finding.severity).empty() || finding.code.empty() ||
        finding.message.empty()) {
      return absl::InvalidArgumentError("curation review contains an invalid finding");
    }
  }
  return absl::OkStatus();
}

absl::Status PublishCurationReview(const CurationReview& review, const std::string& output_path) {
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return PublishNewDirectoryAtomically(
      output_path, [&review](const std::filesystem::path& staging) -> absl::Status {
        nlohmann::json artifacts = nlohmann::json::array();
        for (const CurationArtifact& artifact : review.artifacts) {
          const std::filesystem::path artifact_path = staging / artifact.relative_path;
          RETURN_IF_ERROR(WritePng(artifact_path.string(), artifact.image.width,
                                   artifact.image.height, artifact.image.pixels));
          ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(artifact.image));
          artifacts.push_back({
              {"id", artifact.id},
              {"path", artifact.relative_path},
              {"description", artifact.description},
              {"width", artifact.image.width},
              {"height", artifact.image.height},
              {"rgba_sha256", digest},
              {"metadata", artifact.metadata},
          });
        }

        nlohmann::json findings = nlohmann::json::array();
        for (const CurationFinding& finding : review.findings) {
          findings.push_back({
              {"severity", CurationFindingSeverityId(finding.severity)},
              {"code", finding.code},
              {"subject", finding.subject},
              {"message", finding.message},
          });
        }
        const nlohmann::json manifest = {
            {"schema_version", kCurationReviewSchemaVersion},
            {"kind", review.kind},
            {"asset_id", review.asset_id},
            {"asset_name", review.asset_name},
            {"metadata", review.metadata},
            {"artifacts", std::move(artifacts)},
            {"findings", std::move(findings)},
        };
        return WriteTextFileAtomically((staging / "manifest.json").string(), manifest.dump(2));
      });
}

}  // namespace zebes
