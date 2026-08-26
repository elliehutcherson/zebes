#include "editor/source_artwork_retention.h"

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "api/source_artwork_retention.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "generation/image_generation_request_controller.h"

namespace zebes {

absl::StatusOr<std::string> RetainGeneratedSourceArtwork(Api& api, std::string name,
                                                         const ImageGenerationReview& review,
                                                         const ImageGenerationCandidate& candidate,
                                                         const RetainedSourceAcceptor& accept) {
  SourceArtworkProvenance provenance = GeneratedArtworkProvenance{
      .provider = review.provider,
      .model = review.model,
      .submitted_prompt = review.submitted_prompt,
      .revised_prompt = candidate.revised_prompt,
      .provider_request_id = review.provider_request_id,
      .generated_at_utc = review.generated_at_utc,
  };
  return RetainSourceArtwork(api, std::move(name), std::move(provenance), candidate.image, accept);
}

}  // namespace zebes
