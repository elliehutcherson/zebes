#include <cctype>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "artwork/puppet_build_json.h"
#include "artwork/puppet_document.h"
#include "artwork/puppet_document_json.h"
#include "artwork/puppet_editor_page.h"
#include "artwork/puppet_editor_session.h"
#include "artwork/puppet_editor_workspace.h"
#include "common/loopback_http_server.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

// The defaults are the only place puppets are authored today, so the editor
// runs with no flags at all from the repository root. Each one is still a flag
// because a second character will want its own directories.
ABSL_FLAG(std::string, document_root, "experiments/character_binding/puppet_documents",
          "Directory of puppet documents to choose between.");
ABSL_FLAG(std::string, document, "",
          "Document to open at startup: a file name inside --document_root, or a path whose "
          "directory becomes the root when --document_root is not given.");
ABSL_FLAG(std::string, asset_root, "experiments/character_binding/inputs",
          "Directory the document's source and guide images are resolved against.");
ABSL_FLAG(std::string, rig_root, "experiments/character_binding/inputs",
          "Directory an import_skeleton command's rig_path is resolved against.");
ABSL_FLAG(std::string, page, "",
          "Editor HTML to serve instead of the embedded page, for live editing.");
ABSL_FLAG(int, port, 8770, "Loopback HTTP port.");

namespace {

constexpr size_t kMaximumBodyBytes = 8 * 1024 * 1024;
constexpr std::string_view kJson = "application/json; charset=utf-8";
constexpr std::string_view kPng = "image/png";
constexpr std::string_view kText = "text/plain; charset=utf-8";

zebes::LoopbackHttpResponse Text(int status, std::string_view message) {
  return {.status = status, .content_type = std::string(kText), .body = std::string(message)};
}

zebes::LoopbackHttpResponse Bytes(const std::vector<uint8_t>& bytes) {
  return {.status = 200,
          .content_type = std::string(kPng),
          .body = std::string(bytes.begin(), bytes.end())};
}

// What the browser needs after any edit: the document it should redraw, whether
// the artwork behind it is currently buildable, and the artwork and rigs it can
// offer to choose from. The choices are cheap to gather and change when a file
// appears on disk, so they ride along rather than needing their own poll.
std::string SessionJson(const zebes::PuppetEditorWorkspace& workspace) {
  const zebes::PuppetEditorSession* const open = workspace.session();
  if (open == nullptr) {
    const nlohmann::json empty = {
        {"documents", workspace.AvailableDocuments()},
        {"open_document", ""},
        {"ready", false},
        {"blocker", "open or create a document"},
    };
    return empty.dump();
  }
  const zebes::PuppetEditorSession& session = *open;
  nlohmann::json rigs = nlohmann::json::array();
  for (const zebes::PuppetEditorSession::AvailableRig& rig : session.AvailableRigs()) {
    rigs.push_back({{"path", rig.path}, {"clips", rig.clips}});
  }
  nlohmann::json images = nlohmann::json::array();
  for (const zebes::PuppetEditorSession::AvailableImage& image : session.AvailableImages()) {
    images.push_back({{"path", image.path}, {"width", image.width}, {"height", image.height}});
  }
  nlohmann::json parts = nlohmann::json::array();
  if (session.built() != nullptr) {
    for (const zebes::LayeredPuppetPart& part : session.built()->parts) parts.push_back(part.name);
  }
  nlohmann::json shared = nlohmann::json::array();
  for (const zebes::PuppetEditorSession::SharedPixels& pair : session.OverlappingParts()) {
    shared.push_back({{"first", pair.first}, {"second", pair.second}, {"pixels", pair.pixels}});
  }
  const nlohmann::json payload = {
      {"documents", workspace.AvailableDocuments()},
      {"open_document", workspace.open_document()},
      {"document", nlohmann::json::parse(zebes::PuppetDocumentToJson(session.document()))},
      {"ready", session.built() != nullptr},
      {"blocker", session.build_blocker()},
      {"build_generation", session.build_generation()},
      {"images", std::move(images)},
      {"rigs", std::move(rigs)},
      {"built_parts", std::move(parts)},
      {"shared_pixels", std::move(shared)},
  };
  return payload.dump();
}

// Part names reach this process from a URL, so the accepted set stays narrow
// enough that no name can escape into a path.
bool IsSafeName(std::string_view name) {
  if (name.empty()) return false;
  return std::all_of(name.begin(), name.end(), [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character == '-';
  });
}

zebes::LoopbackHttpResponse ServePage(const std::filesystem::path& page) {
  if (page.empty()) {
    return {.status = 200,
            .content_type = "text/html; charset=utf-8",
            .body = std::string(zebes::PuppetEditorPageHtml())};
  }
  std::ifstream stream(page, std::ios::binary);
  if (!stream.is_open()) return Text(404, absl::StrCat("could not open ", page.string(), "\n"));
  return {.status = 200,
          .content_type = "text/html; charset=utf-8",
          .body = std::string((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>())};
}

// Every POST is refused from another origin. A page served elsewhere must not
// be able to drive an editor bound to this machine, and the browser sends
// Origin on every cross-origin POST.
bool AllowedOrigin(const zebes::LoopbackHttpRequest& request, std::string_view allowed) {
  const auto origin = request.headers.find("origin");
  return origin != request.headers.end() && origin->second == allowed;
}

// Reads the one string field a workspace request carries.
absl::StatusOr<std::string> RequestedName(const zebes::LoopbackHttpRequest& request) {
  try {
    const nlohmann::json body = nlohmann::json::parse(request.body);
    if (!body.is_object() || !body.contains("name") || !body.at("name").is_string()) {
      return absl::InvalidArgumentError("expected a JSON object with a 'name' string");
    }
    return body.at("name").get<std::string>();
  } catch (const std::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat("invalid request body: ", error.what()));
  }
}

zebes::LoopbackHttpResponse HandleCommands(zebes::PuppetEditorWorkspace& workspace,
                                           const zebes::LoopbackHttpRequest& request,
                                           const std::filesystem::path& rig_root) {
  if (workspace.session() == nullptr) return Text(409, "no document is open\n");
  if (request.body.empty()) return Text(411, "a command list is required\n");

  const absl::StatusOr<std::vector<zebes::puppet_edit::Command>> commands =
      zebes::ParsePuppetCommands(request.body, rig_root);
  if (!commands.ok()) return Text(400, commands.status().message());

  const absl::Status applied = workspace.session()->ApplyCommands(*commands);
  if (!applied.ok()) return Text(409, applied.message());
  return {.status = 200, .content_type = std::string(kJson), .body = SessionJson(workspace)};
}

// A skeleton leaves the document as a Rig Bench file, which is the only way one
// built here reaches the next character. It carries joints, chains, bones and
// poses and nothing about this drawing.
zebes::LoopbackHttpResponse HandleSaveRig(zebes::PuppetEditorWorkspace& workspace,
                                          const zebes::LoopbackHttpRequest& request) {
  if (workspace.session() == nullptr) return Text(409, "no document is open\n");
  const absl::StatusOr<std::string> name = RequestedName(request);
  if (!name.ok()) return Text(400, name.status().message());

  std::string clip = "authored";
  bool update_references = true;
  try {
    const nlohmann::json body = nlohmann::json::parse(request.body);
    if (body.contains("clip") && body.at("clip").is_string()) {
      clip = body.at("clip").get<std::string>();
    }
    if (body.contains("update_references") && body.at("update_references").is_boolean()) {
      update_references = body.at("update_references").get<bool>();
    }
  } catch (const std::exception& error) {
    return Text(400, absl::StrCat("invalid request body: ", error.what()));
  }

  const absl::StatusOr<zebes::PuppetEditorWorkspace::RigUpdate> update =
      workspace.SaveRig(*name, clip, update_references);
  if (!update.ok()) return Text(409, update.status().message());
  nlohmann::json payload = nlohmann::json::parse(SessionJson(workspace));
  payload["rig_update"] = {
      {"changed", update->changed}, {"unchanged", update->unchanged}, {"blocked", update->blocked}};
  return {.status = 200, .content_type = std::string(kJson), .body = payload.dump()};
}

zebes::LoopbackHttpResponse HandleOpenOrCreate(zebes::PuppetEditorWorkspace& workspace,
                                               const zebes::LoopbackHttpRequest& request,
                                               bool creating) {
  const absl::StatusOr<std::string> name = RequestedName(request);
  if (!name.ok()) return Text(400, name.status().message());
  const absl::Status done = creating ? workspace.CreateDocument(*name) : workspace.Open(*name);
  if (!done.ok()) return Text(409, done.message());
  return {.status = 200, .content_type = std::string(kJson), .body = SessionJson(workspace)};
}

zebes::LoopbackHttpResponse Handle(zebes::PuppetEditorWorkspace& workspace,
                                   const zebes::LoopbackHttpRequest& request,
                                   std::string_view allowed_origin,
                                   const std::filesystem::path& rig_root,
                                   const std::filesystem::path& page) {
  if (request.target == "/api/commands" || request.target == "/api/open" ||
      request.target == "/api/create" || request.target == "/api/save_rig") {
    if (request.method != "POST") return Text(405, "method not allowed\n");
    if (!AllowedOrigin(request, allowed_origin)) return Text(403, "invalid origin\n");
    if (request.target == "/api/commands") return HandleCommands(workspace, request, rig_root);
    if (request.target == "/api/save_rig") return HandleSaveRig(workspace, request);
    return HandleOpenOrCreate(workspace, request, request.target == "/api/create");
  }
  if (request.method != "GET") return Text(405, "method not allowed\n");

  if (request.target == "/" || request.target == "/editor.html") return ServePage(page);
  if (request.target == "/api/session") {
    return {.status = 200, .content_type = std::string(kJson), .body = SessionJson(workspace)};
  }

  zebes::PuppetEditorSession* const session = workspace.session();
  if (session == nullptr) return Text(404, "no document is open\n");

  if (request.target == "/api/build") {
    if (session->built() == nullptr) return Text(404, session->build_blocker());
    return {.status = 200,
            .content_type = std::string(kJson),
            .body = zebes::LayeredPuppetGeometryJson(*session->built())};
  }
  if (request.target == "/source.png" || request.target == "/guide.png") {
    const absl::StatusOr<std::vector<uint8_t>> encoded = request.target == "/source.png"
                                                             ? session->EncodedSourceImage()
                                                             : session->EncodedGuideImage();
    if (!encoded.ok()) return Text(404, encoded.status().message());
    return Bytes(*encoded);
  }

  constexpr std::string_view kPartsPrefix = "/parts/";
  constexpr std::string_view kPngSuffix = ".png";
  if (absl::StartsWith(request.target, kPartsPrefix) &&
      absl::EndsWith(request.target, kPngSuffix)) {
    const std::string_view name =
        std::string_view(request.target)
            .substr(kPartsPrefix.size(),
                    request.target.size() - kPartsPrefix.size() - kPngSuffix.size());
    if (!IsSafeName(name)) return Text(404, "not found\n");
    const absl::StatusOr<std::vector<uint8_t>> encoded = session->EncodedPart(name);
    if (!encoded.ok()) return Text(404, encoded.status().message());
    return Bytes(*encoded);
  }
  return Text(404, "not found\n");
}

absl::Status Run() {
  const std::filesystem::path document = absl::GetFlag(FLAGS_document);
  std::filesystem::path document_root = absl::GetFlag(FLAGS_document_root);
  if (document_root.empty()) {
    if (document.empty()) {
      return absl::InvalidArgumentError("--document_root or --document is required");
    }
    // One document named on its own still gives a directory to browse, which is
    // the one it sits in.
    document_root = document.parent_path();
  }
  // Only the file name of --document is ever used, so a path pointing somewhere
  // else would quietly open a same-named document in the root instead.
  if (!document.parent_path().empty() && document.parent_path() != document_root) {
    return absl::InvalidArgumentError(absl::StrCat(
        "--document ", document.string(), " is not in --document_root ", document_root.string()));
  }

  const int port = absl::GetFlag(FLAGS_port);
  const std::filesystem::path asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::filesystem::path rig_root = absl::GetFlag(FLAGS_rig_root);
  const std::filesystem::path page = absl::GetFlag(FLAGS_page);
  // The roots default to where this repository keeps its artwork, so a missing
  // one almost always means the server was started somewhere other than the
  // repository root. Say that rather than serving an editor with nothing in it.
  for (const std::filesystem::path& root : {asset_root, rig_root}) {
    if (std::filesystem::is_directory(root)) continue;
    return absl::NotFoundError(
        absl::StrCat(root.string(),
                     " is not a directory; run from the repository root or pass --asset_root "
                     "and --rig_root"));
  }

  ASSIGN_OR_RETURN(std::unique_ptr<zebes::PuppetEditorWorkspace> workspace,
                   zebes::PuppetEditorWorkspace::Create({
                       .document_root = document_root,
                       .asset_root = asset_root,
                       .rig_root = rig_root,
                   }));
  if (!document.empty()) RETURN_IF_ERROR(workspace->Open(document.filename().string()));

  const std::string allowed_origin = absl::StrCat("http://127.0.0.1:", port);
  std::cout << "Serving puppet editor from " << document_root << " on " << allowed_origin << "/\n";
  if (workspace->session() == nullptr) {
    std::cout << "no document open; pick one in the editor\n";
  } else if (workspace->session()->built() != nullptr) {
    std::cout << workspace->open_document() << ": ready to build\n";
  } else {
    std::cout << workspace->open_document() << ": next: " << workspace->session()->build_blocker()
              << '\n';
  }
  std::cout.flush();
  return zebes::RunLoopbackHttpServer(
      port, kMaximumBodyBytes,
      [&workspace, &allowed_origin, &rig_root, &page](const zebes::LoopbackHttpRequest& request) {
        return Handle(*workspace, request, allowed_origin, rig_root, page);
      });
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);
  if (positional.size() != 1) {
    std::cerr << "unexpected positional arguments\n";
    return 2;
  }
  const absl::Status status = Run();
  if (!status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  return 0;
}
