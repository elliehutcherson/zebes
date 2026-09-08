#include "artwork/puppet_editor_session.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "artwork/layered_puppet.h"
#include "artwork/layered_puppet_spec.h"
#include "artwork/puppet_document.h"
#include "artwork/puppet_document_json.h"
#include "artwork/skeleton_rig.h"
#include "common/image_io.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

// A listed path is offered to the browser and comes back as a document field,
// so it stays to characters a URL and a filesystem agree on, and never walks
// upward out of the directory it was found in.
bool IsUrlSafeRelativePath(std::string_view path) {
  if (path.front() == '/' || path.find("..") != std::string_view::npos) return false;
  return std::all_of(path.begin(), path.end(), [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character == '-' ||
           character == '.' || character == '/';
  });
}

// A rig name becomes a file beside the rigs this editor imports from, so it
// stays to characters that cannot name anything outside that directory.
bool IsSafeRigName(std::string_view name) {
  if (name.empty()) return false;
  return std::all_of(name.begin(), name.end(), [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character == '-';
  });
}

absl::StatusOr<std::vector<uint8_t>> EncodeFile(const std::filesystem::path& path) {
  ASSIGN_OR_RETURN(const RgbaImage image, ReadPng(path.string()));
  return EncodePng(image);
}

}  // namespace

absl::StatusOr<std::unique_ptr<PuppetEditorSession>> PuppetEditorSession::Create(
    const PuppetEditorSessionOptions& options) {
  if (options.document_path.empty()) {
    return absl::InvalidArgumentError("a puppet editor session needs a document path");
  }
  ASSIGN_OR_RETURN(PuppetDocument document, LoadPuppetDocument(options.document_path));
  std::unique_ptr<PuppetEditorSession> session(
      new PuppetEditorSession(options, std::move(document)));
  // A document that will not build still opens, carrying the reason as its
  // blocker. Refusing would make the states a person most needs to repair —
  // artwork replaced at another resolution, a source file renamed — reachable
  // only by hand-editing the file this editor exists to replace. An edit that
  // breaks the build is still rolled back; this is only about getting in.
  const absl::Status built = session->RebuildIfStale();
  if (!built.ok()) session->build_blocker_ = std::string(built.message());
  return session;
}

absl::Status PuppetEditorSession::ApplyCommands(absl::Span<const puppet_edit::Command> commands) {
  // The batch lands on a copy so a command that fails halfway leaves both the
  // in-memory document and the file on disk exactly as they were.
  PuppetDocument draft = document_;
  for (size_t index = 0; index < commands.size(); ++index) {
    const absl::Status applied = ApplyPuppetCommand(draft, commands[index]);
    if (!applied.ok()) {
      return absl::Status(applied.code(),
                          absl::StrCat("command[", index, "]: ", applied.message()));
    }
  }
  PuppetDocument previous = std::move(document_);
  document_ = std::move(draft);
  const absl::Status rebuilt = RebuildIfStale();
  if (rebuilt.ok()) return SavePuppetDocument(options_.document_path, document_);

  // The batch is rejected, so the session goes back to the document it had.
  // Whatever that document's own rebuild says becomes the blocker, but the
  // caller is told why its batch was refused: reporting the restore instead
  // answers a question nobody asked and hides the one that was.
  document_ = std::move(previous);
  const absl::Status restored = RebuildIfStale();
  if (!restored.ok()) build_blocker_ = std::string(restored.message());
  return rebuilt;
}

absl::Status PuppetEditorSession::RebuildIfStale() {
  const absl::Status ready = PuppetDocumentReadyToBuild(document_);
  if (!ready.ok()) {
    build_.reset();
    built_key_.clear();
    build_blocker_ = std::string(ready.message());
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(const std::string spec, PuppetDocumentSpecJson(document_));
  // The spec says nothing about which file the pixels came from, so the source
  // path joins it in the staleness key. Without it, pointing the document at
  // different artwork would keep serving the artwork built from the old file.
  const std::filesystem::path source_path = options_.asset_root / document_.source_image;
  const std::string key = absl::StrCat(source_path.string(), "\n", spec);
  if (build_.has_value() && key == built_key_) {
    build_blocker_.clear();
    return absl::OkStatus();
  }

  if (!source_.IsValid() || source_path.string() != source_path_) {
    ASSIGN_OR_RETURN(source_, ReadPng(source_path.string()));
    source_path_ = source_path.string();
  }
  ASSIGN_OR_RETURN(LayeredPuppetSpecBuild build,
                   BuildLayeredPuppetFromSpec(source_, spec, nullptr));
  build_ = std::move(build);
  built_key_ = key;
  ++build_generation_;
  build_blocker_.clear();
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> PuppetEditorSession::EncodedPart(std::string_view name) const {
  if (!build_.has_value()) {
    return absl::NotFoundError("no puppet has been built yet");
  }
  const auto found =
      std::find_if(build_->puppet.parts.begin(), build_->puppet.parts.end(),
                   [name](const LayeredPuppetPart& part) { return part.name == name; });
  if (found == build_->puppet.parts.end()) {
    return absl::NotFoundError(absl::StrCat("unknown part '", name, "'"));
  }
  return EncodePng(found->artwork);
}

absl::StatusOr<std::vector<uint8_t>> PuppetEditorSession::EncodedSourceImage() const {
  if (document_.source_image.empty()) {
    return absl::NotFoundError("this document names no source image");
  }
  return EncodeFile(options_.asset_root / document_.source_image);
}

std::vector<PuppetEditorSession::AvailableImage> PuppetEditorSession::AvailableImages() const {
  std::vector<AvailableImage> images;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator entry(options_.asset_root, error), end;
       entry != end && !error; entry.increment(error)) {
    if (!entry->is_regular_file() || entry->path().extension() != ".png") continue;
    const std::string relative =
        std::filesystem::relative(entry->path(), options_.asset_root, error).generic_string();
    if (error || relative.empty() || !IsUrlSafeRelativePath(relative)) continue;

    const auto known = image_sizes_.find(relative);
    if (known != image_sizes_.end()) {
      images.push_back(known->second);
      continue;
    }
    // A picture that will not decode is left out rather than listed with a size
    // of zero, which the editor would then write into a document.
    const absl::StatusOr<RgbaImage> decoded = ReadPng(entry->path().string());
    if (!decoded.ok()) continue;
    const AvailableImage listed{
        .path = relative, .width = decoded->width, .height = decoded->height};
    image_sizes_.emplace(relative, listed);
    images.push_back(listed);
  }
  std::sort(images.begin(), images.end(),
            [](const AvailableImage& first, const AvailableImage& second) {
              return first.path < second.path;
            });
  return images;
}

std::vector<PuppetEditorSession::AvailableRig> PuppetEditorSession::AvailableRigs() const {
  std::vector<AvailableRig> rigs;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator entry(options_.rig_root, error), end;
       entry != end && !error; entry.increment(error)) {
    if (!entry->is_regular_file() || entry->path().extension() != ".json") continue;
    const std::string relative =
        std::filesystem::relative(entry->path(), options_.rig_root, error).generic_string();
    if (error || relative.empty() || !IsUrlSafeRelativePath(relative)) continue;
    const absl::StatusOr<SkeletonRig> rig = LoadSkeletonRig(entry->path());
    if (!rig.ok()) continue;
    AvailableRig listed{.path = relative};
    for (const SkeletonRigClip& clip : rig->clips) listed.clips.push_back(clip.id);
    if (listed.clips.empty()) continue;
    rigs.push_back(std::move(listed));
  }
  std::sort(rigs.begin(), rigs.end(), [](const AvailableRig& first, const AvailableRig& second) {
    return first.path < second.path;
  });
  return rigs;
}

std::vector<PuppetEditorSession::SharedPixels> PuppetEditorSession::OverlappingParts() const {
  std::vector<SharedPixels> shared;
  if (!build_.has_value()) return shared;

  // visible_artwork is the source pixels a part actually took, which is the
  // ownership question. artwork also holds invented underpaint, and two parts
  // painting the same hidden pixel is not a conflict.
  const std::vector<LayeredPuppetPart>& parts = build_->puppet.parts;
  for (size_t first = 0; first + 1 < parts.size(); ++first) {
    if (!parts[first].visible_artwork.IsValid()) continue;
    for (size_t second = first + 1; second < parts.size(); ++second) {
      const RgbaImage& one = parts[first].visible_artwork;
      const RgbaImage& other = parts[second].visible_artwork;
      if (!other.IsValid() || one.width != other.width || one.height != other.height) continue;
      size_t pixels = 0;
      for (size_t offset = 3; offset < one.pixels.size(); offset += 4) {
        if (one.pixels[offset] != 0 && other.pixels[offset] != 0) ++pixels;
      }
      if (pixels > 0) {
        shared.push_back(
            {.first = parts[first].name, .second = parts[second].name, .pixels = pixels});
      }
    }
  }
  std::sort(shared.begin(), shared.end(), [](const SharedPixels& one, const SharedPixels& other) {
    return one.pixels > other.pixels;
  });
  return shared;
}

absl::Status PuppetEditorSession::SaveRig(std::string_view name, std::string_view clip_id) const {
  if (!IsSafeRigName(name)) {
    return absl::InvalidArgumentError(
        absl::StrCat("'", name, "' is not a rig name; use letters, digits, _ or -"));
  }
  ASSIGN_OR_RETURN(const std::string encoded, PuppetDocumentRigJson(document_, clip_id));
  const std::filesystem::path path = options_.rig_root / absl::StrCat(name, ".json");
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not create ", path.string()));
  }
  stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
  if (!stream.good()) return absl::InternalError(absl::StrCat("could not write ", path.string()));
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> PuppetEditorSession::EncodedGuideImage() const {
  if (document_.guide_image.empty()) {
    return absl::NotFoundError("this document names no guide image");
  }
  return EncodeFile(options_.asset_root / document_.guide_image);
}

}  // namespace zebes
