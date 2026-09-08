#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"

namespace zebes {

// A minimal blocking HTTP/1.1 server bound to 127.0.0.1, for local authoring
// tools whose browser page and backing process are the same person's machine.
// It is not hardened for anything else: one request per connection, no TLS, no
// concurrency, and no authentication beyond being unreachable from the network.

struct LoopbackHttpRequest {
  std::string method;
  std::string target;
  // Header names are lowercased; a repeated header is rejected before the
  // handler runs, so a lookup here has one unambiguous value.
  absl::flat_hash_map<std::string, std::string> headers;
  std::string body;
};

struct LoopbackHttpResponse {
  int status = 200;
  std::string content_type = "text/plain; charset=utf-8";
  // Bytes, not text. Binary payloads such as PNG travel here unchanged.
  std::string body;
};

// Handles one parsed request. Called on the accepting thread, so a slow handler
// blocks the next request.
using LoopbackHttpHandler = std::function<LoopbackHttpResponse(const LoopbackHttpRequest&)>;

// Serves until the process is killed, so it only returns on a socket failure.
// A request whose body exceeds maximum_body_bytes is refused with 413 and the
// handler never sees it.
absl::Status RunLoopbackHttpServer(int port, size_t maximum_body_bytes,
                                   const LoopbackHttpHandler& handler);

}  // namespace zebes
