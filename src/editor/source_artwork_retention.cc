#include "editor/source_artwork_retention.h"

#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "editor/image_generation/image_generation_request_controller.h"

namespace zebes {

absl::StatusOr<std::string> RetainSourceArtwork(Api& api, std::string name,
                                                SourceArtworkProvenance provenance,
                                                const RgbaImage& pixels,
                                                const RetainedSourceAcceptor& accept) {
  if (!accept) {
    return absl::InvalidArgumentError("retained source artwork needs an acceptance callback");
  }
  ASSIGN_OR_RETURN(const std::string id,
                   api.CreateSourceArtwork(std::move(name), std::move(provenance), pixels));
  auto remove_source = absl::MakeCleanup([&api, &id] {
    const absl::Status cleanup = api.DeleteSourceArtwork(id);
    if (!cleanup.ok()) LOG(ERROR) << "Could not remove rejected retained source: " << cleanup;
  });

  ASSIGN_OR_RETURN(SourceArtwork * source, api.GetSourceArtwork(id));
  RETURN_IF_ERROR(accept(*source, pixels));

  std::move(remove_source).Cancel();
  return id;
}

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
