#include "common/loopback_http_server.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

constexpr size_t kMaximumHeaderBytes = 32 * 1024;

std::string_view StatusText(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 409:
      return "Conflict";
    case 411:
      return "Length Required";
    case 413:
      return "Payload Too Large";
    default:
      return "Internal Server Error";
  }
}

#if !defined(_WIN32)
absl::Status WriteAll(int socket, std::string_view bytes) {
  while (!bytes.empty()) {
    const ssize_t written = send(socket, bytes.data(), bytes.size(), 0);
    if (written < 0) {
      if (errno == EINTR) continue;
      return absl::InternalError("could not write HTTP response");
    }
    bytes.remove_prefix(static_cast<size_t>(written));
  }
  return absl::OkStatus();
}

absl::Status Respond(int socket, const LoopbackHttpResponse& response) {
  const std::string headers = absl::StrCat(
      "HTTP/1.1 ", response.status, " ", StatusText(response.status),
      "\r\nContent-Type: ", response.content_type, "\r\nContent-Length: ", response.body.size(),
      "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  RETURN_IF_ERROR(WriteAll(socket, headers));
  return WriteAll(socket, response.body);
}

absl::StatusOr<LoopbackHttpRequest> ReadRequest(int socket, size_t maximum_body_bytes) {
  std::string encoded;
  encoded.reserve(4096);
  size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    if (encoded.size() >= kMaximumHeaderBytes) {
      return absl::ResourceExhaustedError("HTTP headers exceed their limit");
    }
    char buffer[4096];
    const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
    if (received < 0) {
      if (errno == EINTR) continue;
      return absl::InternalError("could not read HTTP request");
    }
    if (received == 0) return absl::DataLossError("HTTP request ended before headers");
    encoded.append(buffer, static_cast<size_t>(received));
    header_end = encoded.find("\r\n\r\n");
  }

  const size_t request_line_end = encoded.find("\r\n");
  if (request_line_end == std::string::npos) {
    return absl::InvalidArgumentError("HTTP request line is missing");
  }
  const std::string request_line = encoded.substr(0, request_line_end);
  const size_t first_space = request_line.find(' ');
  const size_t second_space = request_line.find(' ', first_space + 1);
  if (first_space == std::string::npos || second_space == std::string::npos ||
      request_line.substr(second_space + 1) != "HTTP/1.1") {
    return absl::InvalidArgumentError("HTTP request line is invalid");
  }
  // A query string is the browser's business, not the handler's: pages append
  // one to defeat caching, and every route here is a plain path.
  std::string target = request_line.substr(first_space + 1, second_space - first_space - 1);
  target = target.substr(0, target.find('?'));
  LoopbackHttpRequest request{
      .method = request_line.substr(0, first_space),
      .target = std::move(target),
  };

  size_t line_start = request_line_end + 2;
  while (line_start < header_end) {
    const size_t line_end = encoded.find("\r\n", line_start);
    if (line_end == std::string::npos || line_end > header_end) {
      return absl::InvalidArgumentError("HTTP header is unterminated");
    }
    const std::string_view line(encoded.data() + line_start, line_end - line_start);
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      return absl::InvalidArgumentError("HTTP header is invalid");
    }
    std::string name(line.substr(0, colon));
    absl::AsciiStrToLower(&name);
    std::string value(line.substr(colon + 1));
    absl::StripAsciiWhitespace(&value);
    if (!request.headers.emplace(std::move(name), std::move(value)).second) {
      return absl::InvalidArgumentError("HTTP request repeats a header");
    }
    line_start = line_end + 2;
  }

  size_t content_length = 0;
  if (const auto found = request.headers.find("content-length"); found != request.headers.end()) {
    if (!absl::SimpleAtoi(found->second, &content_length)) {
      return absl::InvalidArgumentError("HTTP content-length is invalid");
    }
  }
  if (content_length > maximum_body_bytes) {
    return absl::ResourceExhaustedError("HTTP body exceeds its limit");
  }
  const size_t body_start = header_end + 4;
  while (encoded.size() - body_start < content_length) {
    char buffer[4096];
    const size_t remaining = content_length - (encoded.size() - body_start);
    const ssize_t received = recv(socket, buffer, std::min(remaining, sizeof(buffer)), 0);
    if (received < 0) {
      if (errno == EINTR) continue;
      return absl::InternalError("could not read HTTP body");
    }
    if (received == 0) return absl::DataLossError("HTTP body ended early");
    encoded.append(buffer, static_cast<size_t>(received));
  }
  request.body = encoded.substr(body_start, content_length);
  return request;
}
#endif

}  // namespace

absl::Status RunLoopbackHttpServer(int port, size_t maximum_body_bytes,
                                   const LoopbackHttpHandler& handler) {
#if defined(_WIN32)
  return absl::UnimplementedError("the loopback HTTP server is not implemented on Windows");
#else
  if (port <= 0 || port > 65535) {
    return absl::InvalidArgumentError("the server port must be within [1, 65535]");
  }
  // A client that hangs up mid-response would otherwise kill the process.
  std::signal(SIGPIPE, SIG_IGN);
  const int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) return absl::InternalError("could not create the server socket");
  const int reuse = 1;
  if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    close(server);
    return absl::InternalError("could not configure the server socket");
  }
  const sockaddr_in address{
      .sin_family = AF_INET,
      .sin_port = htons(static_cast<uint16_t>(port)),
      .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)},
  };
  if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(server, 16) != 0) {
    close(server);
    return absl::InternalError("could not bind the loopback port");
  }
  while (true) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) continue;
      close(server);
      return absl::InternalError("could not accept a connection");
    }
    absl::StatusOr<LoopbackHttpRequest> request = ReadRequest(client, maximum_body_bytes);
    if (!request.ok()) {
      static_cast<void>(
          Respond(client, {.status = absl::IsResourceExhausted(request.status()) ? 413 : 400,
                           .body = std::string(request.status().message())}));
    } else {
      static_cast<void>(Respond(client, handler(*request)));
    }
    close(client);
  }
#endif
}

}  // namespace zebes
