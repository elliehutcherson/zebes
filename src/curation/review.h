#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "common/image_io.h"
#include "nlohmann/json.hpp"

namespace zebes {

inline constexpr int kCurationReviewSchemaVersion = 1;

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
};

absl::Status ValidateCurationReview(const CurationReview& review);

// Publishes a complete review through a sibling staging directory. Existing
// output is never replaced, and manifest.json appears only with the complete
// image set.
absl::Status PublishCurationReview(const CurationReview& review, const std::string& output_path);

std::string_view CurationFindingSeverityId(CurationFindingSeverity severity);

}  // namespace zebes
