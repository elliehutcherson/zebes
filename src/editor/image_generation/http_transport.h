#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/image_generation/credential_source.h"

namespace zebes {

enum class HttpMethod : uint8_t {
  kPost = 0,
};

struct HttpHeader {
  std::string name;
  std::string value;
};

struct HttpSensitiveHeader {
  std::string name;
  SecretString value;
};

struct HttpRequest {
  HttpMethod method = HttpMethod::kPost;
  std::string url;
  std::vector<HttpHeader> headers;
  std::vector<HttpSensitiveHeader> sensitive_headers;
  std::vector<uint8_t> body;
  std::chrono::milliseconds connect_timeout{5000};
  std::chrono::milliseconds total_timeout{120000};
  size_t maximum_response_bytes = 32 * 1024 * 1024;
};

struct HttpResponse {
  int status_code = 0;
  std::vector<HttpHeader> headers;
  std::vector<uint8_t> body;
};

absl::Status ValidateHttpRequest(const HttpRequest& request);
absl::Status ValidateHttpResponse(const HttpResponse& response);

class HttpOperation {
 public:
  virtual ~HttpOperation() = default;

  // Poll must not block. Cancel must return promptly even if the peer does not
  // respond; implementations must not join remote work from either call.
  virtual absl::StatusOr<std::optional<HttpResponse>> Poll() = 0;
  virtual void Cancel() noexcept = 0;
};

class HttpRequestHandle {
 public:
  static absl::StatusOr<HttpRequestHandle> Create(std::unique_ptr<HttpOperation> operation);

  ~HttpRequestHandle();
  HttpRequestHandle(HttpRequestHandle&& other) noexcept;
  HttpRequestHandle& operator=(HttpRequestHandle&& other) noexcept;

  HttpRequestHandle(const HttpRequestHandle&) = delete;
  HttpRequestHandle& operator=(const HttpRequestHandle&) = delete;

  absl::StatusOr<std::optional<HttpResponse>> Poll();
  void Cancel() noexcept;
  bool active() const { return operation_ != nullptr; }

 private:
  friend class HttpTransport;

  explicit HttpRequestHandle(std::unique_ptr<HttpOperation> operation)
      : operation_(std::move(operation)) {}

  std::unique_ptr<HttpOperation> operation_;
  size_t maximum_response_bytes_ = std::numeric_limits<size_t>::max();
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;

  absl::StatusOr<HttpRequestHandle> Start(HttpRequest request);

 protected:
  // Concrete transports must enforce the request bounds while receiving, so
  // an oversized response is stopped before its complete body is allocated.
  virtual absl::StatusOr<HttpRequestHandle> StartValidated(HttpRequest request) = 0;
};

}  // namespace zebes
