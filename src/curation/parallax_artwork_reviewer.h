#pragma once

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "curation/registry.h"

namespace zebes {

class ParallaxArtworkReviewer : public CurationReviewer {
 public:
  std::string_view kind() const override { return "parallax-artwork"; }

  absl::StatusOr<CurationReview> Review(Api& api,
                                        const CurationReviewRequest& request) const override;
  absl::StatusOr<CurationReview> ReviewCandidate(Api& api, const CurationReviewRequest& request,
                                                 const nlohmann::json& candidate) const override;
  absl::Status CommitCandidate(Api& api, const CurationReviewRequest& request,
                               const nlohmann::json& candidate) const override;
};

}  // namespace zebes
