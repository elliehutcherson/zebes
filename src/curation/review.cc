#include "curation/review.h"

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "common/atomic_directory_publisher.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "resources/resource_utils.h"

namespace zebes {
namespace {

bool IsSafeArtifactPath(const std::string& relative_path) {
  const std::filesystem::path path(relative_path);
  if (path.empty() || path.is_absolute() || path.extension() != ".png") return false;
  for (const std::filesystem::path& component : path) {
    if (component == "." || component == "..") return false;
  }
  return path.lexically_normal() == path;
}

absl::Status ValidateReviewHeader(const CurationReview& review) {
  if (review.kind.empty() || review.asset_id.empty() || review.asset_name.empty()) {
    return absl::InvalidArgumentError("curation review needs a kind, asset ID, and asset name");
  }
  if (!review.metadata.is_object()) {
    return absl::InvalidArgumentError("curation review metadata must be an object");
  }
  for (const CurationFinding& finding : review.findings) {
    if (CurationFindingSeverityId(finding.severity).empty() || finding.code.empty() ||
        finding.message.empty()) {
      return absl::InvalidArgumentError("curation review contains an invalid finding");
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateArtifact(const CurationArtifact& artifact) {
  if (artifact.id.empty() || artifact.description.empty() || !artifact.image.IsValid() ||
      !artifact.metadata.is_object() || !IsSafeArtifactPath(artifact.relative_path)) {
    return absl::InvalidArgumentError("curation review contains an invalid artifact");
  }
  return absl::OkStatus();
}

nlohmann::json FindingsToJson(const std::vector<CurationFinding>& review_findings) {
  nlohmann::json findings = nlohmann::json::array();
  for (const CurationFinding& finding : review_findings) {
    findings.push_back({
        {"severity", CurationFindingSeverityId(finding.severity)},
        {"code", finding.code},
        {"subject", finding.subject},
        {"message", finding.message},
    });
  }
  return findings;
}

nlohmann::json ManifestJson(const CurationReview& review, nlohmann::json artifacts) {
  return {
      {"schema_version", kCurationReviewSchemaVersion},
      {"kind", review.kind},
      {"asset_id", review.asset_id},
      {"asset_name", review.asset_name},
      {"metadata", review.metadata},
      {"artifacts", std::move(artifacts)},
      {"findings", FindingsToJson(review.findings)},
  };
}

class StreamingArtifactSink final : public CurationArtifactSink {
 public:
  explicit StreamingArtifactSink(std::filesystem::path staging) : staging_(std::move(staging)) {}

  absl::Status Add(const CurationArtifact& artifact) override {
    RETURN_IF_ERROR(ValidateArtifact(artifact));
    if (!ids_.insert(artifact.id).second || !paths_.insert(artifact.relative_path).second) {
      return absl::InvalidArgumentError("curation artifact IDs and paths must be unique");
    }
    const int64_t pixels = static_cast<int64_t>(artifact.image.width) * artifact.image.height;
    if (pixels > kMaximumCurationReviewPixels - total_pixels_) {
      return absl::ResourceExhaustedError("curation review exceeds its total pixel limit");
    }
    total_pixels_ += pixels;

    const std::filesystem::path artifact_path = staging_ / artifact.relative_path;
    RETURN_IF_ERROR(WritePng(artifact_path.string(), artifact.image.width, artifact.image.height,
                             artifact.image.pixels));
    ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(artifact.image));
    artifacts_.push_back({
        {"id", artifact.id},
        {"path", artifact.relative_path},
        {"description", artifact.description},
        {"width", artifact.image.width},
        {"height", artifact.image.height},
        {"rgba_sha256", digest},
        {"metadata", artifact.metadata},
    });
    return absl::OkStatus();
  }

  absl::Status Finish(const CurationReview& review) const {
    RETURN_IF_ERROR(ValidateReviewHeader(review));
    if (!review.artifacts.empty()) {
      return absl::InvalidArgumentError(
          "streamed curation producer must not retain in-memory artifacts");
    }
    if (artifacts_.empty()) {
      return absl::InvalidArgumentError("curation review must contain an artifact");
    }
    return WriteTextFileAtomically((staging_ / "manifest.json").string(),
                                   ManifestJson(review, artifacts_).dump(2));
  }

  size_t artifact_count() const { return artifacts_.size(); }

 private:
  std::filesystem::path staging_;
  std::set<std::string> ids_;
  std::set<std::string> paths_;
  int64_t total_pixels_ = 0;
  nlohmann::json artifacts_ = nlohmann::json::array();
};

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
  RETURN_IF_ERROR(ValidateReviewHeader(review));
  if (review.artifacts.empty()) {
    return absl::InvalidArgumentError("curation review must contain an artifact");
  }

  std::set<std::string> ids;
  std::set<std::string> paths;
  int64_t total_pixels = 0;
  for (const CurationArtifact& artifact : review.artifacts) {
    RETURN_IF_ERROR(ValidateArtifact(artifact));
    if (!ids.insert(artifact.id).second || !paths.insert(artifact.relative_path).second) {
      return absl::InvalidArgumentError("curation artifact IDs and paths must be unique");
    }
    total_pixels += static_cast<int64_t>(artifact.image.width) * artifact.image.height;
    if (total_pixels > kMaximumCurationReviewPixels) {
      return absl::ResourceExhaustedError("curation review exceeds its total pixel limit");
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

        const nlohmann::json manifest = ManifestJson(review, std::move(artifacts));
        return WriteTextFileAtomically((staging / "manifest.json").string(), manifest.dump(2));
      });
}

absl::StatusOr<size_t> PublishCurationReviewStreamed(const std::string& output_path,
                                                     StreamedCurationReviewProducer producer) {
  if (!producer) return absl::InvalidArgumentError("streamed curation producer is missing");
  size_t artifact_count = 0;
  RETURN_IF_ERROR(PublishNewDirectoryAtomically(
      output_path,
      [producer = std::move(producer),
       &artifact_count](const std::filesystem::path& staging) mutable -> absl::Status {
        StreamingArtifactSink sink(staging);
        CurationReview review;
        RETURN_IF_ERROR(std::move(producer)(sink, review));
        RETURN_IF_ERROR(sink.Finish(review));
        artifact_count = sink.artifact_count();
        return absl::OkStatus();
      }));
  return artifact_count;
}

}  // namespace zebes
