#include "common/utc_timestamp.h"

#include <string>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace zebes {

std::string CurrentUtcTimestamp() {
  return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", absl::Now(), absl::UTCTimeZone());
}

}  // namespace zebes
