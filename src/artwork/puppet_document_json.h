#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/puppet_document.h"

namespace zebes {

// Reading and writing the two documents the puppet editor exchanges: the
// authored document itself, and the list of commands that edits it. Both are
// strict: every field is required and an unknown field is an error, so a
// renamed field cannot be read as an addition and a dropped one cannot be read
// as a default.

// Parses schema-version-1 document JSON. The result has already passed
// ValidatePuppetDocument, so no caller receives a document whose frames and
// skeleton disagree.
absl::StatusOr<PuppetDocument> ParsePuppetDocument(std::string_view encoded);

// Canonical schema-version-1 JSON with deterministic key ordering, so saving an
// unchanged document produces an unchanged file.
std::string PuppetDocumentToJson(const PuppetDocument& document);

absl::StatusOr<PuppetDocument> LoadPuppetDocument(const std::filesystem::path& path);
absl::Status SavePuppetDocument(const std::filesystem::path& path, const PuppetDocument& document);

// Parses a JSON array of commands, each an object whose "command" field selects
// the edit and whose remaining fields are that edit's own. An import_skeleton
// command names a rig file, which is read relative to rig_base_directory, so a
// command list stays a plain data file with no embedded skeleton.
absl::StatusOr<std::vector<puppet_edit::Command>> ParsePuppetCommands(
    std::string_view encoded, const std::filesystem::path& rig_base_directory);

// Projects a build-ready document into the layered puppet spec JSON that
// BuildLayeredPuppetFromSpec consumes. This is derived output: it is handed
// straight to the builder and is not something a person edits or keeps. Fails
// with the PuppetDocumentReadyToBuild message when the document is not finished.
absl::StatusOr<std::string> PuppetDocumentSpecJson(const PuppetDocument& document);

// Writes the document's skeleton out as Rig Bench schema-version-2 JSON, so a
// skeleton built here can be imported onto the next character. Only joints,
// chains, bones and poses travel; artwork, parts and outlines belong to the one
// drawing and are left behind.
//
// The document's frames become the named clip. A document with no frames yet is
// the ordinary case for a rig authored on its own, and gives a one-frame clip
// holding the rest pose, which is what import_skeleton reads a rest pose from.
//
// floor_y is the lowest joint, because a Rig Bench file requires one and the
// document has no ground of its own.
absl::StatusOr<std::string> PuppetDocumentRigJson(const PuppetDocument& document,
                                                  std::string_view clip_id);

}  // namespace zebes
