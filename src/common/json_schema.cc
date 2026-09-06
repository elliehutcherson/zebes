#include "common/json_schema.h"

#include <initializer_list>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

namespace zebes::json_schema {

absl::Status RequireExactObject(const nlohmann::json& json,
                                std::initializer_list<std::string_view> keys,
                                std::string_view context) {
  if (!json.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an object"));
  }
  absl::flat_hash_set<std::string_view> expected(keys);
  for (const auto& [key, unused_value] : json.items()) {
    static_cast<void>(unused_value);
    if (!expected.contains(key)) {
      return absl::InvalidArgumentError(
          absl::StrCat(context, " contains unknown field '", key, "'"));
    }
  }
  // Reported in the caller's declaration order so the message is stable across
  // runs; the hash set's iteration order is not.
  for (std::string_view key : keys) {
    if (!json.contains(std::string(key))) {
      return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
    }
  }
  return absl::OkStatus();
}

}  // namespace zebes::json_schema
