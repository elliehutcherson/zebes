#pragma once

#include <functional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "editor/image_generation/image_generation_request_controller.h"

namespace zebes {

using RetainedSourceAcceptor = std::function<absl::Status(const SourceArtwork&, const RgbaImage&)>;

// Creates and reloads retained source artwork before exposing it to a domain
// editor. If reload or domain acceptance fails, the new source is deleted.
absl::StatusOr<std::string> RetainSourceArtwork(Api& api, std::string name,
                                                SourceArtworkProvenance provenance,
                                                const RgbaImage& pixels,
                                                const RetainedSourceAcceptor& accept);

absl::StatusOr<std::string> RetainGeneratedSourceArtwork(Api& api, std::string name,
                                                         const ImageGenerationReview& review,
                                                         const ImageGenerationCandidate& candidate,
                                                         const RetainedSourceAcceptor& accept);

}  // namespace zebes
