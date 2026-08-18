#include "editor/image_generation/http_transport.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

bool ContainsLineBreak(absl::string_view value) {
  return value.find('\r') != absl::string_view::npos || value.find('\n') != absl::string_view::npos;
}

absl::Status ValidateHeader(absl::string_view name, absl::string_view value) {
  if (name.empty()) return absl::InvalidArgumentError("HTTP header name must not be empty");
  if (ContainsLineBreak(name) || ContainsLineBreak(value)) {
    return absl::InvalidArgumentError("HTTP headers must not contain line breaks");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateHttpRequest(const HttpRequest& request) {
  if (request.method != HttpMethod::kPost) {
    return absl::InvalidArgumentError("HTTP request method is invalid");
  }
  if (!absl::StartsWith(request.url, "https://")) {
    return absl::InvalidArgumentError("HTTP request URL must use HTTPS");
  }
  if (ContainsLineBreak(request.url)) {
    return absl::InvalidArgumentError("HTTP request URL must not contain line breaks");
  }
  if (request.connect_timeout <= std::chrono::milliseconds::zero() ||
      request.total_timeout <= std::chrono::milliseconds::zero()) {
    return absl::InvalidArgumentError("HTTP request timeouts must be positive");
  }
  if (request.connect_timeout > request.total_timeout) {
    return absl::InvalidArgumentError("HTTP connect timeout exceeds total timeout");
  }
  if (request.maximum_response_bytes == 0) {
    return absl::InvalidArgumentError("HTTP response byte limit must be positive");
  }
  for (const HttpHeader& header : request.headers) {
    RETURN_IF_ERROR(ValidateHeader(header.name, header.value));
  }
  for (const HttpSensitiveHeader& header : request.sensitive_headers) {
    RETURN_IF_ERROR(ValidateHeader(header.name, header.value.value()));
  }
  return absl::OkStatus();
}

absl::Status ValidateHttpResponse(const HttpResponse& response) {
  if (response.status_code < 100 || response.status_code > 599) {
    return absl::InvalidArgumentError("HTTP response status code is invalid");
  }
  for (const HttpHeader& header : response.headers) {
    RETURN_IF_ERROR(ValidateHeader(header.name, header.value));
  }
  return absl::OkStatus();
}

absl::StatusOr<HttpRequestHandle> HttpRequestHandle::Create(
    std::unique_ptr<HttpOperation> operation) {
  if (operation == nullptr) return absl::InvalidArgumentError("HTTP request needs an operation");
  return HttpRequestHandle(std::move(operation));
}

HttpRequestHandle::~HttpRequestHandle() { Cancel(); }

HttpRequestHandle::HttpRequestHandle(HttpRequestHandle&& other) noexcept
    : operation_(std::move(other.operation_)),
      maximum_response_bytes_(other.maximum_response_bytes_) {}

HttpRequestHandle& HttpRequestHandle::operator=(HttpRequestHandle&& other) noexcept {
  if (this == &other) return *this;
  Cancel();
  operation_ = std::move(other.operation_);
  maximum_response_bytes_ = other.maximum_response_bytes_;
  return *this;
}

absl::StatusOr<std::optional<HttpResponse>> HttpRequestHandle::Poll() {
  if (operation_ == nullptr) {
    return absl::FailedPreconditionError("HTTP request is no longer active");
  }
  bool should_cancel = true;
  absl::Cleanup finish_request = [this, &should_cancel] {
    if (should_cancel) operation_->Cancel();
    operation_.reset();
  };
  ASSIGN_OR_RETURN(std::optional<HttpResponse> response, operation_->Poll());
  if (!response.has_value()) {
    std::move(finish_request).Cancel();
    return std::nullopt;
  }
  if (response->body.size() > maximum_response_bytes_) {
    return absl::ResourceExhaustedError("HTTP response exceeded its byte limit");
  }
  const absl::Status valid = ValidateHttpResponse(*response);
  if (!valid.ok()) {
    return absl::DataLossError(
        absl::StrCat("HTTP transport returned an invalid response: ", valid.message()));
  }
  std::optional<HttpResponse> finished(std::move(*response));
  should_cancel = false;
  return finished;
}

void HttpRequestHandle::Cancel() noexcept {
  if (operation_ == nullptr) return;
  operation_->Cancel();
  operation_.reset();
}

absl::StatusOr<HttpRequestHandle> HttpTransport::Start(HttpRequest request) {
  RETURN_IF_ERROR(ValidateHttpRequest(request));
  const size_t maximum_response_bytes = request.maximum_response_bytes;
  ASSIGN_OR_RETURN(HttpRequestHandle handle, StartValidated(std::move(request)));
  handle.maximum_response_bytes_ = maximum_response_bytes;
  return handle;
}

}  // namespace zebes
