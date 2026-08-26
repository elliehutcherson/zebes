#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "api/source_artwork_retention.h"
#include "generation/image_generation_request_controller.h"

namespace zebes {

absl::StatusOr<std::string> RetainGeneratedSourceArtwork(Api& api, std::string name,
                                                         const ImageGenerationReview& review,
                                                         const ImageGenerationCandidate& candidate,
                                                         const RetainedSourceAcceptor& accept);

}  // namespace zebes
