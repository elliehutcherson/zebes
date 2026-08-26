#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "curation/review.h"

namespace zebes {

class CurationReviewer {
 public:
  virtual ~CurationReviewer() = default;

  virtual std::string_view kind() const = 0;
  virtual absl::StatusOr<CurationReview> Review(Api& api,
                                                const CurationReviewRequest& request) const = 0;

  // Candidate documents are kind-owned. The generic command can review and
  // explicitly commit one, while an adapter refuses formats that cannot be
  // persisted safely as a single definition (for example, a generated prop
  // bundle that must be regenerated transactionally).
  virtual absl::StatusOr<CurationReview> ReviewCandidate(Api& api,
                                                         const CurationReviewRequest& request,
                                                         const nlohmann::json& candidate) const;
  virtual absl::Status CommitCandidate(Api& api, const CurationReviewRequest& request,
                                       const nlohmann::json& candidate) const;
};

// Stable-ID dispatch keeps the command generic. Supporting another asset kind
// means adding one reviewer; publication, manifests, safety limits, and command
// behavior do not change.
class CurationRegistry {
 public:
  absl::Status Add(std::unique_ptr<CurationReviewer> reviewer);

  absl::StatusOr<CurationReview> Review(Api& api, std::string_view kind,
                                        const CurationReviewRequest& request) const;

  absl::StatusOr<CurationReview> ReviewCandidate(Api& api, std::string_view kind,
                                                 const CurationReviewRequest& request,
                                                 const nlohmann::json& candidate) const;
  absl::Status CommitCandidate(Api& api, std::string_view kind,
                               const CurationReviewRequest& request,
                               const nlohmann::json& candidate) const;

  std::vector<std::string> Kinds() const;

 private:
  std::map<std::string, std::unique_ptr<CurationReviewer>, std::less<>> reviewers_;
};

}  // namespace zebes
