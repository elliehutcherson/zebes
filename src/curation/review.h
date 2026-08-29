#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "nlohmann/json.hpp"

namespace zebes {

inline constexpr int kCurationReviewSchemaVersion = 1;
inline constexpr int64_t kMaximumCurationReviewPixels = 512LL * 1024 * 1024;

enum class CurationFindingSeverity {
  kInfo,
  kWarning,
};

struct CurationFinding {
  CurationFindingSeverity severity = CurationFindingSeverity::kInfo;
  std::string code;
  std::string subject;
  std::string message;
};

struct CurationArtifact {
  std::string id;
  std::string relative_path;
  std::string description;
  RgbaImage image;
  nlohmann::json metadata = nlohmann::json::object();
};

struct CurationReview {
  std::string kind;
  std::string asset_id;
  std::string asset_name;
  nlohmann::json metadata = nlohmann::json::object();
  std::vector<CurationArtifact> artifacts;
  std::vector<CurationFinding> findings;
};

struct CurationReviewRequest {
  std::string asset_id;
  // Directory containing a candidate document's sibling artifacts. Empty for
  // persisted reviews and recipe-only candidates.
  std::string candidate_root;
  // Limits a persisted level review to deterministic cameras centered on one
  // placed entity. Other reviewer kinds reject this option at the dispatch
  // boundary rather than silently ignoring it.
  std::optional<uint64_t> focus_entity_id;
};

// Synchronously consumes one artifact. A streamed producer may release or
// reuse the image as soon as Add returns; the sink never retains decoded RGBA
// pixels after it has encoded and recorded the artifact.
class CurationArtifactSink {
 public:
  virtual ~CurationArtifactSink() = default;
  virtual absl::Status Add(const CurationArtifact& artifact) = 0;
};

// Populates the review header/findings and emits every artifact through the
// sink. The producer must leave review.artifacts empty because streamed
// artifacts are already owned by the atomic staging directory.
using StreamedCurationReviewProducer =
    absl::AnyInvocable<absl::Status(CurationArtifactSink& sink, CurationReview& review)>;

absl::Status ValidateCurationReview(const CurationReview& review);

// Publishes a complete review through a sibling staging directory. Existing
// output is never replaced, and manifest.json appears only with the complete
// image set.
absl::Status PublishCurationReview(const CurationReview& review, const std::string& output_path);

// Publishes a review while bounding decoded-image memory to the producer's
// working set. PNGs are written into the same private atomic staging directory
// as the manifest, and the final directory appears only after the producer and
// all validation complete. Returns the published artifact count.
absl::StatusOr<size_t> PublishCurationReviewStreamed(const std::string& output_path,
                                                     StreamedCurationReviewProducer producer);

std::string_view CurationFindingSeverityId(CurationFindingSeverity severity);

}  // namespace zebes
