#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "artwork/layered_puppet.h"
#include "artwork/layered_puppet_spec.h"
#include "artwork/puppet_document.h"
#include "common/image_io.h"

namespace zebes {

struct PuppetEditorSessionOptions {
  // The document this session reads at startup and writes after every accepted
  // batch of commands.
  std::filesystem::path document_path;
  // Directory the document's source_image and guide_image are resolved against.
  std::filesystem::path asset_root;
  // Directory an import_skeleton command's rig_path is resolved against.
  std::filesystem::path rig_root;
};

// One puppet being edited, holding the document and the artwork last built from
// it. Everything the browser and the headless CLI can do to a puppet happens
// here, so the HTTP layer above stays a translation of requests into commands.
//
// Commands are applied as one batch. Either every command in the batch lands
// and the document is saved, or none of them do and the document on disk is
// untouched, so a browser that sends a bad edit cannot leave a half-edited file
// behind.
//
// Artwork is rebuilt only when the build inputs actually changed. Moving a
// joint or adding a frame changes no pixels and skips the rebuild; changing an
// outline, a part, or the source image forces one. A document that is not
// finished yet holds no artwork at all, and build_blocker() says what it still
// needs.
//
// Opening never fails on a build failure, only on a document that will not
// parse. A document naming a missing picture, or one whose coordinates are not
// the picture's size, opens with no artwork and its reason in build_blocker(),
// because those are the states someone opens the editor to fix.
class PuppetEditorSession {
 public:
  static absl::StatusOr<std::unique_ptr<PuppetEditorSession>> Create(
      const PuppetEditorSessionOptions& options);

  const PuppetDocument& document() const { return document_; }

  // Applies every command, then saves and rebuilds. On failure the message
  // names the command that failed by its position in the batch.
  absl::Status ApplyCommands(absl::Span<const puppet_edit::Command> commands);

  // The puppet built from the current document, or null when the document is
  // not finished. Rebuilt in place, so the reference is invalidated by the next
  // ApplyCommands call.
  const LayeredPuppet* built() const { return build_.has_value() ? &build_->puppet : nullptr; }

  // Empty while built() is non-null; otherwise the next thing the document
  // needs, phrased for the author.
  const std::string& build_blocker() const { return build_blocker_; }

  // Counts rebuilds. The browser holds a copy of the geometry and only needs a
  // fresh one when this changes, so a joint drag costs no mesh transfer.
  size_t build_generation() const { return build_generation_; }

  // PNG bytes for one built part, or NotFound when the part does not exist or
  // nothing has been built. Encoded on demand rather than cached, because a
  // part is fetched once per rebuild and the browser holds it afterwards.
  absl::StatusOr<std::vector<uint8_t>> EncodedPart(std::string_view name) const;

  // PNG bytes for the document's source or guide artwork, read from disk under
  // asset_root. NotFound when the document names none.
  absl::StatusOr<std::vector<uint8_t>> EncodedSourceImage() const;
  absl::StatusOr<std::vector<uint8_t>> EncodedGuideImage() const;

  // Two parts claiming the same source pixel, with how many pixels they share.
  // Both draw it, and in motion they carry it in different directions, so it
  // tears. Nothing in the still source shows this — the composite looks whole
  // because the overlap is exactly where the parts still coincide. Empty when
  // nothing has been built. exclude_parts is the fix.
  struct SharedPixels {
    std::string first;
    std::string second;
    size_t pixels = 0;
  };
  std::vector<SharedPixels> OverlappingParts() const;

  // Writes the skeleton to rig_root as a Rig Bench file, which is how a
  // skeleton built here reaches the next character. Needs joints and bones and
  // nothing else, so a document with no artwork yet can still produce one.
  // Replaces a rig of the same name: importing copies a skeleton into the
  // document that imports it, so no existing puppet changes meaning when the
  // file behind it does. PuppetEditorWorkspace::SaveRig is what carries an
  // update into the documents that imported it.
  absl::Status SaveRig(std::string_view name, std::string_view clip_id) const;

  // One picture the editor can offer, with the size it actually is. The size
  // travels with the name because a document's width and height are the
  // coordinate space every joint and outline is written in: picking a picture
  // and leaving the old size behind would silently misplace all of them.
  struct AvailableImage {
    std::string path;
    int width = 0;
    int height = 0;
  };

  // Pictures under asset_root, relative to it and sorted. Only PNG, and only
  // names a URL can carry unescaped. Sizes are read once and remembered, so
  // listing them after every edit costs nothing.
  std::vector<AvailableImage> AvailableImages() const;

  // Rig files under rig_root, paired with the clips each one holds, so the
  // editor can offer a skeleton and a clip together rather than separately. A
  // file that does not parse is left out rather than failing the listing: it is
  // one bad file among the others, not a broken editor.
  struct AvailableRig {
    std::string path;
    std::vector<std::string> clips;
  };
  std::vector<AvailableRig> AvailableRigs() const;

 private:
  PuppetEditorSession(PuppetEditorSessionOptions options, PuppetDocument document)
      : options_(std::move(options)), document_(std::move(document)) {}

  // Rebuilds when the source path or the spec derived from the document differ
  // from the pair the current artwork was built from. Comparing that derived
  // pair is what makes "did this edit change any pixels" a question with one
  // answer instead of a rule per command kind.
  absl::Status RebuildIfStale();

  PuppetEditorSessionOptions options_;
  PuppetDocument document_;
  std::string built_key_;
  size_t build_generation_ = 0;
  std::optional<LayeredPuppetSpecBuild> build_;
  std::string build_blocker_;
  RgbaImage source_;
  std::string source_path_;
  mutable absl::flat_hash_map<std::string, AvailableImage> image_sizes_;
};

}  // namespace zebes
