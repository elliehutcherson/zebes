#pragma once

#include <string_view>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "curation/registry.h"

namespace zebes {

class TilesetReviewer : public CurationReviewer {
 public:
  std::string_view kind() const override { return "tileset"; }

  absl::StatusOr<CurationReview> Review(Api& api,
                                        const CurationReviewRequest& request) const override;
};

}  // namespace zebes
