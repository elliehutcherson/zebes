#include "generation/curl_http_transport.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "common/status_macros.h"
#include "curl/curl.h"

namespace zebes {
namespace {

constexpr size_t kMaximumResponseHeaderBytes = 64 * 1024;
using CurlLong = long;  // NOLINT(google-runtime-int): libcurl's C API requires long.

absl::Status EnsureCurlInitialized() {
  static const absl::Status kStatus = [] {
    const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK) {
      return absl::InternalError(
          absl::StrCat("could not initialize HTTP transport: ", curl_easy_strerror(result)));
    }
    const curl_version_info_data* version = curl_version_info(CURLVERSION_NOW);
    if (version == nullptr || (version->features & CURL_VERSION_ASYNCHDNS) == 0) {
      return absl::FailedPreconditionError(
          "libcurl needs asynchronous DNS support for non-blocking editor requests");
    }
    return absl::OkStatus();
  }();
  return kStatus;
}

absl::Status CurlError(CURLcode result) {
  switch (result) {
    case CURLE_OPERATION_TIMEDOUT:
      return absl::DeadlineExceededError("HTTP request reached its total timeout");
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
      return absl::UnavailableError(
          absl::StrCat("HTTP transport failed: ", curl_easy_strerror(result)));
    case CURLE_ABORTED_BY_CALLBACK:
      return absl::CancelledError("HTTP request was cancelled");
    default:
      return absl::InternalError(
          absl::StrCat("HTTP transport failed: ", curl_easy_strerror(result)));
  }
}

absl::Status CurlMultiError(CURLMcode result) {
  return absl::InternalError(
      absl::StrCat("HTTP transport could not advance a request: ", curl_multi_strerror(result)));
}

template <typename Value>
absl::Status SetOption(CURL* easy, CURLoption option, Value value) {
  const CURLcode result = curl_easy_setopt(easy, option, value);
  if (result != CURLE_OK) return CurlError(result);
  return absl::OkStatus();
}

class CurlHttpOperation final : public HttpOperation {
 public:
  static absl::StatusOr<std::unique_ptr<CurlHttpOperation>> Create(HttpRequest request) {
    std::unique_ptr<CurlHttpOperation> operation(new CurlHttpOperation(std::move(request)));
    RETURN_IF_ERROR(operation->Initialize());
    return operation;
  }

  ~CurlHttpOperation() override {
    Cancel();
    if (easy_ != nullptr) curl_easy_cleanup(easy_);
    if (headers_ != nullptr) curl_slist_free_all(headers_);
    if (multi_ != nullptr) curl_multi_cleanup(multi_);
  }

  absl::StatusOr<std::optional<HttpResponse>> Poll() override {
    if (!active_) {
      return absl::FailedPreconditionError("curl HTTP request is no longer active");
    }

    int running = 0;
    const CURLMcode perform = curl_multi_perform(multi_, &running);
    if (perform != CURLM_OK) return CurlMultiError(perform);

    int remaining_messages = 0;
    CURLMsg* message = nullptr;
    while ((message = curl_multi_info_read(multi_, &remaining_messages)) != nullptr) {
      if (message->msg != CURLMSG_DONE || message->easy_handle != easy_) continue;
      Detach();
      RETURN_IF_ERROR(callback_status_);
      if (message->data.result != CURLE_OK) return CurlError(message->data.result);

      CurlLong status_code = 0;
      const CURLcode info = curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status_code);
      if (info != CURLE_OK) return CurlError(info);
      if (!std::in_range<int>(status_code)) {
        return absl::DataLossError("HTTP response status code is out of range");
      }
      response_.status_code = static_cast<int>(status_code);
      return std::optional<HttpResponse>(std::move(response_));
    }
    return std::nullopt;
  }

  void Cancel() noexcept override { Detach(); }

  // curl_multi_timeout answers how long the caller may wait before calling
  // curl_multi_perform again, which is exactly this question. Two of its
  // answers need translating.
  //
  // A negative timeout means curl has no timer pending and expects the caller
  // to wait on the transfer's sockets instead. This transport registers no
  // sockets with its caller, so honouring that literally would sleep until the
  // total timeout. It becomes kPollCap instead. Requests here run for seconds,
  // so that costs a few wakeups per second and at most kPollCap of latency.
  //
  // Zero means curl wants attention immediately and must not be slept on.
  absl::Duration SuggestedPollDelay() const override {
    if (!active_) return absl::ZeroDuration();
    CurlLong timeout_milliseconds = 0;
    if (curl_multi_timeout(multi_, &timeout_milliseconds) != CURLM_OK) {
      return absl::ZeroDuration();
    }
    if (timeout_milliseconds < 0) return CurlHttpTransport::kPollCap;
    return std::min(absl::Milliseconds(timeout_milliseconds), CurlHttpTransport::kPollCap);
  }

 private:
  explicit CurlHttpOperation(HttpRequest request) : request_(std::move(request)) {}

  absl::Status Initialize() {
    if (!std::in_range<CurlLong>(request_.connect_timeout.count()) ||
        !std::in_range<CurlLong>(request_.total_timeout.count())) {
      return absl::OutOfRangeError("HTTP timeout exceeds libcurl's supported range");
    }
    if (!std::in_range<curl_off_t>(request_.body.size())) {
      return absl::OutOfRangeError("HTTP request body exceeds libcurl's supported range");
    }

    easy_ = curl_easy_init();
    multi_ = curl_multi_init();
    if (easy_ == nullptr || multi_ == nullptr) {
      return absl::ResourceExhaustedError("could not allocate a curl HTTP request");
    }

    for (const HttpHeader& header : request_.headers) {
      RETURN_IF_ERROR(AppendHeader(header.name, header.value));
    }
    for (const HttpSensitiveHeader& header : request_.sensitive_headers) {
      RETURN_IF_ERROR(AppendHeader(header.name, header.value.value()));
    }

    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_URL, request_.url.c_str()));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_PROTOCOLS_STR, "https"));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_REDIR_PROTOCOLS_STR, "https"));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_FOLLOWLOCATION, 0L));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_NOSIGNAL, 1L));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_SSL_VERIFYPEER, 1L));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_SSL_VERIFYHOST, 2L));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_CONNECTTIMEOUT_MS,
                              static_cast<CurlLong>(request_.connect_timeout.count())));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_TIMEOUT_MS,
                              static_cast<CurlLong>(request_.total_timeout.count())));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_POST, 1L));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_POSTFIELDSIZE_LARGE,
                              static_cast<curl_off_t>(request_.body.size())));
    const char* post_fields =
        request_.body.empty() ? "" : reinterpret_cast<const char*>(request_.body.data());
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_POSTFIELDS, post_fields));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_HTTPHEADER, headers_));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_WRITEFUNCTION, &CurlHttpOperation::WriteBody));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_WRITEDATA, this));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_HEADERFUNCTION, &CurlHttpOperation::WriteHeader));
    RETURN_IF_ERROR(SetOption(easy_, CURLOPT_HEADERDATA, this));

    const CURLMcode added = curl_multi_add_handle(multi_, easy_);
    if (added != CURLM_OK) return CurlMultiError(added);
    active_ = true;
    return absl::OkStatus();
  }

  absl::Status AppendHeader(std::string_view name, std::string_view value) {
    const std::string line = absl::StrCat(name, ": ", value);
    curl_slist* appended = curl_slist_append(headers_, line.c_str());
    if (appended == nullptr) {
      return absl::ResourceExhaustedError("could not allocate HTTP request headers");
    }
    headers_ = appended;
    return absl::OkStatus();
  }

  static size_t WriteBody(char* data, size_t size, size_t count, void* context) {
    CurlHttpOperation& operation = *static_cast<CurlHttpOperation*>(context);
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
      operation.callback_status_ = absl::ResourceExhaustedError("HTTP response is too large");
      return 0;
    }
    const size_t bytes = size * count;
    if (bytes > operation.request_.maximum_response_bytes - operation.response_.body.size()) {
      operation.callback_status_ =
          absl::ResourceExhaustedError("HTTP response exceeded its byte limit");
      return 0;
    }
    const auto* begin = reinterpret_cast<const uint8_t*>(data);
    operation.response_.body.insert(operation.response_.body.end(), begin, begin + bytes);
    return bytes;
  }

  static size_t WriteHeader(char* data, size_t size, size_t count, void* context) {
    CurlHttpOperation& operation = *static_cast<CurlHttpOperation*>(context);
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
      operation.callback_status_ =
          absl::ResourceExhaustedError("HTTP response headers are too large");
      return 0;
    }
    const size_t bytes = size * count;
    if (bytes > kMaximumResponseHeaderBytes - operation.response_header_bytes_) {
      operation.callback_status_ =
          absl::ResourceExhaustedError("HTTP response headers exceeded their byte limit");
      return 0;
    }
    operation.response_header_bytes_ += bytes;

    std::string_view line(data, bytes);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    const size_t separator = line.find(':');
    if (separator == std::string_view::npos) return bytes;

    std::string_view value = line.substr(separator + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
      value.remove_suffix(1);
    }
    operation.response_.headers.push_back(HttpHeader{
        .name = std::string(line.substr(0, separator)),
        .value = std::string(value),
    });
    return bytes;
  }

  void Detach() noexcept {
    if (!active_) return;
    curl_multi_remove_handle(multi_, easy_);
    active_ = false;
  }

  HttpRequest request_;
  HttpResponse response_;
  absl::Status callback_status_;
  CURL* easy_ = nullptr;
  CURLM* multi_ = nullptr;
  curl_slist* headers_ = nullptr;
  size_t response_header_bytes_ = 0;
  bool active_ = false;
};

}  // namespace

absl::StatusOr<std::unique_ptr<CurlHttpTransport>> CurlHttpTransport::Create() {
  RETURN_IF_ERROR(EnsureCurlInitialized());
  return std::unique_ptr<CurlHttpTransport>(new CurlHttpTransport());
}

absl::StatusOr<HttpRequestHandle> CurlHttpTransport::StartValidated(HttpRequest request) {
  ASSIGN_OR_RETURN(std::unique_ptr<CurlHttpOperation> operation,
                   CurlHttpOperation::Create(std::move(request)));
  return HttpRequestHandle::Create(std::move(operation));
}

}  // namespace zebes
