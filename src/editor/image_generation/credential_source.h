#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace zebes {

// A deliberately non-copyable credential value. The value is exposed only to
// the adapter constructing an authenticated request and is overwritten when
// its owner releases it. This limits accidental copies and logging; it is not
// a promise that every library or operating-system copy can be erased.
class SecretString {
 public:
  static absl::StatusOr<SecretString> Create(std::string value);

  ~SecretString();
  SecretString(SecretString&& other) noexcept;
  SecretString& operator=(SecretString&& other) noexcept;

  SecretString(const SecretString&) = delete;
  SecretString& operator=(const SecretString&) = delete;

  absl::string_view value() const { return value_; }

 private:
  explicit SecretString(std::string value) : value_(std::move(value)) {}

  void Clear() noexcept;

  std::string value_;
};

// Supplies a credential by a non-secret reference such as an environment
// variable name or keychain item ID. Implementations must never include the
// credential value in an error status.
class CredentialSource {
 public:
  virtual ~CredentialSource() = default;

  virtual absl::StatusOr<SecretString> Load(absl::string_view reference) const = 0;
};

class EnvironmentCredentialSource final : public CredentialSource {
 public:
  using Reader = std::function<std::optional<std::string>(absl::string_view variable_name)>;

  EnvironmentCredentialSource();
  explicit EnvironmentCredentialSource(Reader reader) : reader_(std::move(reader)) {}

  absl::StatusOr<SecretString> Load(absl::string_view reference) const override;

 private:
  Reader reader_;
};

}  // namespace zebes
