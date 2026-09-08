#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/puppet_editor_session.h"

namespace zebes {

struct PuppetEditorWorkspaceOptions {
  // Directory holding the puppet documents this workspace can open and create.
  std::filesystem::path document_root;
  std::filesystem::path asset_root;
  std::filesystem::path rig_root;
};

// A directory of puppet documents and whichever one is open. A session owns one
// document; this owns the question of which document that is, so opening
// another is a normal action rather than a restart.
//
// Opening or creating replaces the open session outright. Nothing is carried
// across, because everything a session held was derived from the document it
// had, and the new one derives its own.
//
// A workspace with nothing open is a normal state, not a failure: it is what an
// empty directory looks like, and what the editor shows before a choice is made.
class PuppetEditorWorkspace {
 public:
  static absl::StatusOr<std::unique_ptr<PuppetEditorWorkspace>> Create(
      const PuppetEditorWorkspaceOptions& options);

  // Document file names under the root, sorted. A file that does not parse is
  // still listed, so a broken document can be seen and fixed rather than
  // vanishing from the editor.
  std::vector<std::string> AvailableDocuments() const;

  // The open document's file name, empty when none is open.
  const std::string& open_document() const { return open_document_; }

  // Null when no document is open.
  PuppetEditorSession* session() { return session_.get(); }
  const PuppetEditorSession* session() const { return session_.get(); }

  // Opens an existing document by file name. On failure the previously open
  // session is left untouched, so a bad file cannot close the good one.
  absl::Status Open(std::string_view name);

  // Writes a new empty document and opens it. An empty document is valid; it
  // simply cannot build yet, and the editor says what it needs next. Refuses a
  // name that already exists rather than overwriting someone's work.
  absl::Status CreateDocument(std::string_view name);

  // What updating a rig did to the documents that imported it.
  struct RigUpdate {
    // Documents whose skeleton_source names this rig, and what changed in each.
    // A document that needed nothing is still listed, saying so.
    std::vector<std::string> changed;
    std::vector<std::string> unchanged;
    // Documents the rig no longer covers. A joint they still hang a bone or a
    // part on cannot be dropped without breaking them, so nothing was written.
    std::vector<std::string> blocked;
  };

  // Writes the open document's skeleton to rig_root, replacing an existing rig
  // of that name, then optionally carries the change into every other document
  // that imported it.
  //
  // Carrying it means adding joints the rig gained and bones it gained, and
  // taking the rig's chain for every joint. It never moves a joint that is
  // already there: those positions were dragged onto that character's own
  // artwork and are the binding between rig and drawing. It never removes
  // anything either; a document still using a joint the rig dropped is reported
  // and left alone rather than half-updated.
  absl::StatusOr<RigUpdate> SaveRig(std::string_view name, std::string_view clip_id,
                                    bool update_references);

 private:
  explicit PuppetEditorWorkspace(PuppetEditorWorkspaceOptions options)
      : options_(std::move(options)) {}

  // Resolves a caller-supplied file name to a path inside the root, rejecting
  // anything that could name a file outside it.
  absl::StatusOr<std::filesystem::path> Resolve(std::string_view name) const;

  PuppetEditorWorkspaceOptions options_;
  std::string open_document_;
  std::unique_ptr<PuppetEditorSession> session_;
};

}  // namespace zebes
