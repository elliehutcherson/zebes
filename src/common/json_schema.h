#ifndef ZEBES_COMMON_JSON_SCHEMA_H_
#define ZEBES_COMMON_JSON_SCHEMA_H_

#include <exception>
#include <initializer_list>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

// Strict readers for the serialized definition format, which has no optional
// fields: a missing field is corruption, not a request for a default.
//
// `context` names the record being read and opens every error message, so pass
// the record's name in prose ("prop recipe", "terrain build config") rather
// than a file path or type name.
//
// These live in their own namespace because readers conventionally pull them in
// with a `using` declaration, or wrap them to bind a fixed context. Both would
// be ambiguous against a same-named helper in the reader's own anonymous
// namespace if these sat directly in `zebes`.
namespace zebes::json_schema {

// Reads one required field. Absent, or present with the wrong type, is an
// error either way.
template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& json, std::string_view key,
                           std::string_view context) {
  const std::string field(key);
  if (!json.is_object() || !json.contains(field)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
  }
  try {
    return json.at(field).get<T>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " field '", key, "' is invalid: ", error.what()));
  }
}

// Requires an object whose keys are exactly `keys`. Both an unknown field and
// a missing one are rejected, so a renamed field cannot be read as an addition
// and a dropped one cannot be read as a default.
absl::Status RequireExactObject(const nlohmann::json& json,
                                std::initializer_list<std::string_view> keys,
                                std::string_view context);

}  // namespace zebes::json_schema

#endif  // ZEBES_COMMON_JSON_SCHEMA_H_
