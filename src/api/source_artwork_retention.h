#pragma once

#include <functional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"

namespace zebes {

using RetainedSourceAcceptor = std::function<absl::Status(const SourceArtwork&, const RgbaImage&)>;

// Creates and reloads retained source artwork before exposing it to a domain
// transaction. If reload or domain acceptance fails, the new source is
// deleted. The callback is the only place that may take ownership of the
// retained source by publishing another definition that references it.
absl::StatusOr<std::string> RetainSourceArtwork(Api& api, std::string name,
                                                SourceArtworkProvenance provenance,
                                                const RgbaImage& pixels,
                                                const RetainedSourceAcceptor& accept);

}  // namespace zebes
