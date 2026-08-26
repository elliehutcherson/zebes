#include "api/source_artwork_retention.h"

#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"

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
  if (source == nullptr) {
    return absl::FailedPreconditionError("retained source artwork lookup returned null");
  }
  RETURN_IF_ERROR(accept(*source, pixels));

  std::move(remove_source).Cancel();
  return id;
}

}  // namespace zebes
