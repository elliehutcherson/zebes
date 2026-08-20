#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "editor/image_generation/http_transport.h"

namespace zebes {

// A single-threaded, poll-driven HTTPS transport. Each request owns a libcurl
// multi handle so Poll never waits for socket activity and Cancel can detach an
// in-flight transfer immediately.
class CurlHttpTransport final : public HttpTransport {
 public:
  // The longest SuggestedPollDelay will ever ask a caller to wait, and so the
  // ceiling on how long a finished response can sit unnoticed. This is what
  // makes the transport bounded polling rather than socket-driven wakeups:
  // once a transfer has an established socket, curl stops offering a timer and
  // expects the caller to wait on the socket instead, which this transport
  // does not expose. That case becomes this cap.
  static constexpr absl::Duration kPollCap = absl::Milliseconds(200);

  static absl::StatusOr<std::unique_ptr<CurlHttpTransport>> Create();

 protected:
  absl::StatusOr<HttpRequestHandle> StartValidated(HttpRequest request) override;

 private:
  CurlHttpTransport() = default;
};

}  // namespace zebes
