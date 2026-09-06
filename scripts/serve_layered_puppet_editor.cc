#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "artwork/layered_puppet_editor_state.h"
#include "common/status_macros.h"
#include "resources/resource_utils.h"

ABSL_FLAG(std::string, root, "", "Generated layered-puppet editor asset directory.");
ABSL_FLAG(std::string, state, "", "Tracked authored editor state JSON path.");
ABSL_FLAG(std::string, contract, "", "Generated immutable editor contract JSON path.");
ABSL_FLAG(int, port, 8769, "Loopback HTTP port.");

namespace {

constexpr size_t kMaximumHeaderBytes = 32 * 1024;
constexpr size_t kMaximumStateBytes = 2 * 1024 * 1024;
constexpr size_t kMaximumStaticBytes = 16 * 1024 * 1024;

struct HttpRequest {
  std::string method;
  std::string target;
  absl::flat_hash_map<std::string, std::string> headers;
  std::string body;
};

struct ServerState {
  std::filesystem::path root;
  std::filesystem::path state_path;
  zebes::LayeredPuppetEditorStateContract contract;
  zebes::LayeredPuppetEditorState authored;
  std::string allowed_origin;
};

absl::StatusOr<std::string> ReadBoundedFile(const std::filesystem::path& path,
                                            size_t maximum_bytes) {
  std::error_code error;
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > maximum_bytes) {
    return absl::ResourceExhaustedError(
        absl::StrCat("could not size bounded editor file: ", path.string()));
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open editor file: ", path.string()));
  }
  std::string contents(static_cast<size_t>(size), '\0');
  stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(contents.size())) {
    return absl::DataLossError(
        absl::StrCat("could not read complete editor file: ", path.string()));
  }
  return contents;
}

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
      return absl::InternalError("could not write editor HTTP response");
    }
    bytes.remove_prefix(static_cast<size_t>(written));
  }
  return absl::OkStatus();
}

absl::Status Respond(int socket, int status, std::string_view content_type, std::string_view body) {
  const std::string headers =
      absl::StrCat("HTTP/1.1 ", status, " ", StatusText(status), "\r\nContent-Type: ", content_type,
                   "\r\nContent-Length: ", body.size(),
                   "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  RETURN_IF_ERROR(WriteAll(socket, headers));
  return WriteAll(socket, body);
}

absl::StatusOr<HttpRequest> ReadRequest(int socket) {
  std::string encoded;
  encoded.reserve(4096);
  size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    if (encoded.size() >= kMaximumHeaderBytes) {
      return absl::ResourceExhaustedError("editor HTTP headers exceed their limit");
    }
    char buffer[4096];
    const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
    if (received < 0) {
      if (errno == EINTR) continue;
      return absl::InternalError("could not read editor HTTP request");
    }
    if (received == 0) return absl::DataLossError("editor HTTP request ended before headers");
    encoded.append(buffer, static_cast<size_t>(received));
    header_end = encoded.find("\r\n\r\n");
  }

  const size_t request_line_end = encoded.find("\r\n");
  if (request_line_end == std::string::npos) {
    return absl::InvalidArgumentError("editor HTTP request line is missing");
  }
  const std::string request_line = encoded.substr(0, request_line_end);
  const size_t first_space = request_line.find(' ');
  const size_t second_space = request_line.find(' ', first_space + 1);
  if (first_space == std::string::npos || second_space == std::string::npos ||
      request_line.substr(second_space + 1) != "HTTP/1.1") {
    return absl::InvalidArgumentError("editor HTTP request line is invalid");
  }
  HttpRequest request{
      .method = request_line.substr(0, first_space),
      .target = request_line.substr(first_space + 1, second_space - first_space - 1),
  };

  size_t line_start = request_line_end + 2;
  while (line_start < header_end) {
    const size_t line_end = encoded.find("\r\n", line_start);
    if (line_end == std::string::npos || line_end > header_end) {
      return absl::InvalidArgumentError("editor HTTP header is unterminated");
    }
    const std::string_view line(encoded.data() + line_start, line_end - line_start);
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      return absl::InvalidArgumentError("editor HTTP header is invalid");
    }
    std::string name(line.substr(0, colon));
    absl::AsciiStrToLower(&name);
    std::string value(line.substr(colon + 1));
    absl::StripAsciiWhitespace(&value);
    if (!request.headers.emplace(std::move(name), std::move(value)).second) {
      return absl::InvalidArgumentError("editor HTTP request repeats a header");
    }
    line_start = line_end + 2;
  }

  size_t content_length = 0;
  if (const auto found = request.headers.find("content-length"); found != request.headers.end()) {
    if (!absl::SimpleAtoi(found->second, &content_length)) {
      return absl::InvalidArgumentError("editor HTTP content-length is invalid");
    }
  }
  if (content_length > kMaximumStateBytes) {
    return absl::ResourceExhaustedError("editor HTTP body exceeds its limit");
  }
  const size_t body_start = header_end + 4;
  while (encoded.size() - body_start < content_length) {
    char buffer[4096];
    const size_t remaining = content_length - (encoded.size() - body_start);
    const ssize_t received = recv(socket, buffer, std::min(remaining, sizeof(buffer)), 0);
    if (received < 0) {
      if (errno == EINTR) continue;
      return absl::InternalError("could not read editor HTTP body");
    }
    if (received == 0) return absl::DataLossError("editor HTTP body ended early");
    encoded.append(buffer, static_cast<size_t>(received));
  }
  request.body = encoded.substr(body_start, content_length);
  return request;
}

std::optional<std::filesystem::path> StaticPath(const ServerState& state, std::string_view target) {
  if (target == "/") target = "/editor.html";
  if (target == "/editor.html" || target == "/source.png" || target == "/editor-contract.json") {
    return state.root / std::string(target.substr(1));
  }
  constexpr std::string_view kPartsPrefix = "/parts/";
  if (!absl::StartsWith(target, kPartsPrefix) || !absl::EndsWith(target, ".png")) {
    return std::nullopt;
  }
  const std::string_view name =
      target.substr(kPartsPrefix.size(), target.size() - kPartsPrefix.size() - 4);
  if (name.empty() || std::any_of(name.begin(), name.end(), [](const char character) {
        return !std::isalnum(static_cast<unsigned char>(character)) && character != '_' &&
               character != '-';
      })) {
    return std::nullopt;
  }
  return state.root / "parts" / absl::StrCat(name, ".png");
}

std::string_view ContentType(const std::filesystem::path& path) {
  if (path.extension() == ".html") return "text/html; charset=utf-8";
  if (path.extension() == ".json") return "application/json; charset=utf-8";
  if (path.extension() == ".png") return "image/png";
  return "application/octet-stream";
}

absl::Status HandleRequest(int socket, const HttpRequest& request, ServerState& state) {
  if (request.target == "/api/state") {
    if (request.method == "GET") {
      return Respond(socket, 200, "application/json; charset=utf-8",
                     zebes::LayeredPuppetEditorStateToJson(state.authored));
    }
    if (request.method != "PUT") {
      return Respond(socket, 405, "text/plain; charset=utf-8", "method not allowed\n");
    }
    const auto origin = request.headers.find("origin");
    if (origin == request.headers.end() || origin->second != state.allowed_origin) {
      return Respond(socket, 403, "text/plain; charset=utf-8", "invalid origin\n");
    }
    if (request.body.empty()) {
      return Respond(socket, 411, "text/plain; charset=utf-8", "state body is required\n");
    }
    absl::StatusOr<zebes::LayeredPuppetEditorState> incoming =
        zebes::ParseLayeredPuppetEditorState(request.body);
    if (!incoming.ok()) {
      return Respond(socket, 400, "text/plain; charset=utf-8", incoming.status().message());
    }
    const absl::Status valid = zebes::ValidateLayeredPuppetEditorState(*incoming, state.contract);
    if (!valid.ok()) {
      return Respond(socket, 409, "text/plain; charset=utf-8", valid.message());
    }
    const std::string encoded = zebes::LayeredPuppetEditorStateToJson(*incoming);
    const absl::Status written = zebes::WriteTextFileAtomically(state.state_path.string(), encoded);
    if (!written.ok()) {
      return Respond(socket, 500, "text/plain; charset=utf-8", written.message());
    }
    state.authored = std::move(*incoming);
    return Respond(socket, 200, "application/json; charset=utf-8", encoded);
  }

  if (request.method != "GET") {
    return Respond(socket, 405, "text/plain; charset=utf-8", "method not allowed\n");
  }
  const std::optional<std::filesystem::path> path = StaticPath(state, request.target);
  if (!path.has_value()) {
    return Respond(socket, 404, "text/plain; charset=utf-8", "not found\n");
  }
  const size_t maximum = path->extension() == ".png" ? kMaximumStaticBytes : kMaximumStateBytes;
  absl::StatusOr<std::string> contents = ReadBoundedFile(*path, maximum);
  if (!contents.ok()) {
    return Respond(socket, 404, "text/plain; charset=utf-8", "not found\n");
  }
  return Respond(socket, 200, ContentType(*path), *contents);
}

absl::Status RunServer(ServerState& state, int port) {
  std::signal(SIGPIPE, SIG_IGN);
  const int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) return absl::InternalError("could not create editor socket");
  const int reuse = 1;
  if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    close(server);
    return absl::InternalError("could not configure editor socket");
  }
  const sockaddr_in address{
      .sin_family = AF_INET,
      .sin_port = htons(static_cast<uint16_t>(port)),
      .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)},
  };
  if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(server, 16) != 0) {
    close(server);
    return absl::InternalError("could not bind editor loopback port");
  }
  std::cout << "Serving layered puppet editor on http://127.0.0.1:" << port << "/editor.html\n";
  std::cout.flush();
  while (true) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) continue;
      close(server);
      return absl::InternalError("could not accept editor connection");
    }
    absl::StatusOr<HttpRequest> request = ReadRequest(client);
    if (!request.ok()) {
      static_cast<void>(Respond(client, absl::IsResourceExhausted(request.status()) ? 413 : 400,
                                "text/plain; charset=utf-8", request.status().message()));
    } else {
      static_cast<void>(HandleRequest(client, *request, state));
    }
    close(client);
  }
}
#endif

absl::Status Run() {
#if defined(_WIN32)
  return absl::UnimplementedError("layered puppet editor server is not implemented on Windows");
#else
  const int port = absl::GetFlag(FLAGS_port);
  if (port <= 0 || port > 65535) {
    return absl::InvalidArgumentError("editor port must be within [1, 65535]");
  }
  const std::filesystem::path root = absl::GetFlag(FLAGS_root);
  const std::filesystem::path state_path = absl::GetFlag(FLAGS_state);
  const std::filesystem::path contract_path = absl::GetFlag(FLAGS_contract);
  if (root.empty() || state_path.empty() || contract_path.empty()) {
    return absl::InvalidArgumentError("--root, --state, and --contract are required");
  }
  ASSIGN_OR_RETURN(const std::string contract_json,
                   ReadBoundedFile(contract_path, kMaximumStateBytes));
  ASSIGN_OR_RETURN(zebes::LayeredPuppetEditorStateContract contract,
                   zebes::ParseLayeredPuppetEditorStateContract(contract_json));
  ASSIGN_OR_RETURN(const std::string state_json, ReadBoundedFile(state_path, kMaximumStateBytes));
  ASSIGN_OR_RETURN(zebes::LayeredPuppetEditorState authored,
                   zebes::ParseLayeredPuppetEditorState(state_json));
  RETURN_IF_ERROR(zebes::ValidateLayeredPuppetEditorState(authored, contract));
  ServerState state{
      .root = root,
      .state_path = state_path,
      .contract = std::move(contract),
      .authored = std::move(authored),
      .allowed_origin = absl::StrCat("http://127.0.0.1:", port),
  };
  return RunServer(state, port);
#endif
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  const absl::Status status = Run();
  if (!status.ok()) {
    std::cerr << status << '\n';
    return 1;
  }
  return 0;
}
