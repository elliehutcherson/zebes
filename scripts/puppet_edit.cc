#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "artwork/puppet_document.h"
#include "artwork/puppet_document_json.h"
#include "common/status_macros.h"

ABSL_FLAG(std::string, document, "", "Puppet document JSON to read and write.");
ABSL_FLAG(std::string, commands, "",
          "JSON array of edits to apply, or '-' to read the array from stdin.");
ABSL_FLAG(bool, create, false, "Start from an empty document instead of reading --document.");
ABSL_FLAG(std::string, rig_root, ".",
          "Directory an import_skeleton command's rig_path is resolved against.");
ABSL_FLAG(std::string, emit_spec, "",
          "Optional path to write the derived layered puppet spec JSON.");
ABSL_FLAG(std::string, emit_rig, "",
          "Optional path to write the skeleton out as a Rig Bench file, so it can be imported "
          "onto another character. Needs joints and bones; artwork is not required.");
ABSL_FLAG(std::string, rig_clip, "authored",
          "Clip id inside --emit_rig. The document's frames become that clip, or its rest pose "
          "does when it has no frames.");
ABSL_FLAG(bool, dry_run, false, "Apply and report without writing anything.");

namespace {

absl::StatusOr<std::string> ReadCommands(const std::string& path) {
  if (path == "-") {
    return std::string((std::istreambuf_iterator<char>(std::cin)),
                       std::istreambuf_iterator<char>());
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open command list: ", path));
  }
  std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (!stream.good() && !stream.eof()) {
    return absl::DataLossError(absl::StrCat("could not read command list: ", path));
  }
  return contents;
}

absl::Status WriteText(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not create ", path.string()));
  }
  stream << contents;
  if (!stream.good()) return absl::InternalError(absl::StrCat("could not write ", path.string()));
  return absl::OkStatus();
}

// Applies the edits and reports what the document now holds. Every failure is
// reported against the command that caused it, because a list of twenty edits
// is unreadable when the message only says which field was wrong.
absl::Status Run() {
  const std::string document_path = absl::GetFlag(FLAGS_document);
  const std::string command_path = absl::GetFlag(FLAGS_commands);
  if (document_path.empty()) return absl::InvalidArgumentError("--document is required");

  zebes::PuppetDocument document;
  if (!absl::GetFlag(FLAGS_create)) {
    ASSIGN_OR_RETURN(document, zebes::LoadPuppetDocument(document_path));
  } else if (std::filesystem::exists(document_path)) {
    return absl::AlreadyExistsError(absl::StrCat("--create refuses to overwrite ", document_path));
  }

  if (!command_path.empty()) {
    ASSIGN_OR_RETURN(const std::string encoded, ReadCommands(command_path));
    ASSIGN_OR_RETURN(const std::vector<zebes::puppet_edit::Command> commands,
                     zebes::ParsePuppetCommands(encoded, absl::GetFlag(FLAGS_rig_root)));
    for (size_t index = 0; index < commands.size(); ++index) {
      const absl::Status applied = zebes::ApplyPuppetCommand(document, commands[index]);
      if (!applied.ok()) {
        return absl::Status(applied.code(),
                            absl::StrCat("command[", index, "]: ", applied.message()));
      }
    }
  }

  const absl::Status ready = zebes::PuppetDocumentReadyToBuild(document);
  std::cout << document.rest_pose.size() << " joints, " << document.bones.size() << " bones, "
            << document.parts.size() << " parts, " << document.frames.size() << " frames"
            << (document.anchor_frame.empty() ? ""
                                              : absl::StrCat(", anchor ", document.anchor_frame))
            << '\n'
            << (ready.ok() ? "ready to build" : absl::StrCat("next: ", ready.message())) << '\n';

  const std::string spec_path = absl::GetFlag(FLAGS_emit_spec);
  if (!spec_path.empty() && !ready.ok()) {
    return absl::FailedPreconditionError(absl::StrCat("cannot emit a spec yet: ", ready.message()));
  }
  // A rig needs only a skeleton, so it is emitted from documents a spec cannot
  // come out of. That is the whole point of authoring one on its own.
  const std::string rig_path = absl::GetFlag(FLAGS_emit_rig);
  if (absl::GetFlag(FLAGS_dry_run)) return absl::OkStatus();

  RETURN_IF_ERROR(zebes::SavePuppetDocument(document_path, document));
  if (!rig_path.empty()) {
    ASSIGN_OR_RETURN(const std::string rig,
                     zebes::PuppetDocumentRigJson(document, absl::GetFlag(FLAGS_rig_clip)));
    RETURN_IF_ERROR(WriteText(rig_path, rig));
    std::cout << "wrote rig " << rig_path << '\n';
  }
  if (spec_path.empty()) return absl::OkStatus();
  ASSIGN_OR_RETURN(const std::string spec, zebes::PuppetDocumentSpecJson(document));
  return WriteText(spec_path, absl::StrCat(spec, "\n"));
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);
  if (positional.size() != 1) {
    std::cerr << "unexpected positional arguments\n";
    return 2;
  }
  const absl::Status status = Run();
  if (status.ok()) return 0;
  std::cerr << status.message() << '\n';
  return 1;
}
