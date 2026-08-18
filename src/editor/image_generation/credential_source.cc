#include "editor/image_generation/credential_source.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

bool IsEnvironmentVariableName(absl::string_view name) {
  if (name.empty()) return false;
  const unsigned char first = static_cast<unsigned char>(name.front());
  if (name.front() != '_' && std::isalpha(first) == 0) return false;
  for (const char character : name.substr(1)) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (character != '_' && std::isalnum(value) == 0) return false;
  }
  return true;
}

std::optional<std::string> ReadEnvironment(absl::string_view variable_name) {
  const std::string owned_name(variable_name);
  const char* value = std::getenv(owned_name.c_str());
  if (value == nullptr) return std::nullopt;
  return std::string(value);
}

}  // namespace

absl::StatusOr<SecretString> SecretString::Create(std::string value) {
  if (value.empty()) return absl::InvalidArgumentError("credential value must not be empty");
  return SecretString(std::move(value));
}

SecretString::~SecretString() { Clear(); }

SecretString::SecretString(SecretString&& other) noexcept { value_.swap(other.value_); }

SecretString& SecretString::operator=(SecretString&& other) noexcept {
  if (this == &other) return *this;
  Clear();
  value_.swap(other.value_);
  return *this;
}

void SecretString::Clear() noexcept {
  volatile char* bytes = value_.data();
  for (size_t index = 0; index < value_.size(); ++index) bytes[index] = '\0';
  value_.clear();
}

EnvironmentCredentialSource::EnvironmentCredentialSource() : reader_(ReadEnvironment) {}

absl::StatusOr<SecretString> EnvironmentCredentialSource::Load(absl::string_view reference) const {
  if (!IsEnvironmentVariableName(reference)) {
    return absl::InvalidArgumentError("credential environment variable name is invalid");
  }
  if (!reader_) {
    return absl::FailedPreconditionError("credential environment reader is not configured");
  }
  std::optional<std::string> value = reader_(reference);
  if (!value.has_value() || value->empty()) {
    return absl::UnauthenticatedError(
        absl::StrCat("credential '", reference, "' is not configured"));
  }
  return SecretString::Create(std::move(*value));
}

}  // namespace zebes
