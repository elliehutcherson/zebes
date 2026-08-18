#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "editor/image_generation/http_transport.h"

namespace zebes {

// A single-threaded, poll-driven HTTPS transport. Each request owns a libcurl
// multi handle so Poll never waits for socket activity and Cancel can detach an
// in-flight transfer immediately.
class CurlHttpTransport final : public HttpTransport {
 public:
  static absl::StatusOr<std::unique_ptr<CurlHttpTransport>> Create();

 protected:
  absl::StatusOr<HttpRequestHandle> StartValidated(HttpRequest request) override;

 private:
  CurlHttpTransport() = default;
};

}  // namespace zebes
