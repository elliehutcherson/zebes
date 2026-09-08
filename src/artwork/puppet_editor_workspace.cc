#include "artwork/puppet_editor_workspace.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "artwork/puppet_document.h"
#include "artwork/puppet_document_json.h"
#include "artwork/puppet_editor_session.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

constexpr std::string_view kExtension = ".json";

// Document names travel through a URL and become a path, so the accepted set is
// narrow enough for both and cannot contain a separator or a parent step.
bool IsSafeDocumentName(std::string_view name) {
  if (name.size() <= kExtension.size() || !name.ends_with(kExtension)) return false;
  const std::string_view stem = name.substr(0, name.size() - kExtension.size());
  if (stem.empty()) return false;
  return std::all_of(stem.begin(), stem.end(), [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' || character == '-';
  });
}

}  // namespace

absl::StatusOr<std::unique_ptr<PuppetEditorWorkspace>> PuppetEditorWorkspace::Create(
    const PuppetEditorWorkspaceOptions& options) {
  if (options.document_root.empty()) {
    return absl::InvalidArgumentError("a puppet workspace needs a document directory");
  }
  std::error_code error;
  std::filesystem::create_directories(options.document_root, error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not use ", options.document_root.string(), ": ", error.message()));
  }
  return std::unique_ptr<PuppetEditorWorkspace>(new PuppetEditorWorkspace(options));
}

std::vector<std::string> PuppetEditorWorkspace::AvailableDocuments() const {
  std::vector<std::string> names;
  std::error_code error;
  for (std::filesystem::directory_iterator entry(options_.document_root, error), end;
       entry != end && !error; entry.increment(error)) {
    if (!entry->is_regular_file()) continue;
    const std::string name = entry->path().filename().string();
    if (IsSafeDocumentName(name)) names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

absl::StatusOr<std::filesystem::path> PuppetEditorWorkspace::Resolve(std::string_view name) const {
  if (!IsSafeDocumentName(name)) {
    return absl::InvalidArgumentError(
        absl::StrCat("'", name, "' is not a document name; use letters, digits, _ or - and .json"));
  }
  return options_.document_root / std::string(name);
}

absl::Status PuppetEditorWorkspace::Open(std::string_view name) {
  ASSIGN_OR_RETURN(const std::filesystem::path path, Resolve(name));
  ASSIGN_OR_RETURN(std::unique_ptr<PuppetEditorSession> opened,
                   PuppetEditorSession::Create({.document_path = path,
                                                .asset_root = options_.asset_root,
                                                .rig_root = options_.rig_root}));
  session_ = std::move(opened);
  open_document_ = std::string(name);
  return absl::OkStatus();
}

absl::StatusOr<PuppetEditorWorkspace::RigUpdate> PuppetEditorWorkspace::SaveRig(
    std::string_view name, std::string_view clip_id, bool update_references) {
  if (session_ == nullptr) return absl::FailedPreconditionError("no document is open");
  RETURN_IF_ERROR(session_->SaveRig(name, clip_id));

  RigUpdate update;
  if (!update_references) return update;

  const std::string rig_file = absl::StrCat(name, ".json");
  const PuppetDocument& rig = session_->document();
  for (const std::string& document_name : AvailableDocuments()) {
    if (document_name == open_document_) continue;
    const std::filesystem::path path = options_.document_root / document_name;
    const absl::StatusOr<PuppetDocument> other = LoadPuppetDocument(path);
    // A file that will not parse is not this operation's problem, and failing
    // the whole update over one would leave the rest half-carried.
    if (!other.ok() || other->skeleton_source.rig_path != rig_file) continue;

    std::vector<std::string> dropped;
    for (const auto& [joint, chain] : other->joint_chains) {
      if (!rig.rest_pose.contains(joint)) dropped.push_back(joint);
    }
    if (!dropped.empty()) {
      update.blocked.push_back(document_name);
      continue;
    }

    std::vector<puppet_edit::Command> commands;
    for (const auto& [joint, rest] : rig.rest_pose) {
      if (!other->rest_pose.contains(joint)) {
        // At the rig's own coordinates, which are not this character's. It has
        // to be dragged onto the drawing like any imported joint.
        commands.push_back(puppet_edit::AddJoint{
            .name = joint, .rest = rest, .chain = rig.joint_chains.at(joint)});
        continue;
      }
      const auto chain = other->joint_chains.find(joint);
      if (chain != other->joint_chains.end() && chain->second != rig.joint_chains.at(joint)) {
        commands.push_back(
            puppet_edit::SetJointChain{.name = joint, .chain = rig.joint_chains.at(joint)});
      }
    }
    for (const PuppetDocumentBone& bone : rig.bones) {
      // Matched on the joints a bone runs between, not its name. A Rig Bench
      // file stores no bone names, so an imported skeleton calls this one
      // "hip-knee" where the rig it came from called it "thigh"; matching on
      // names would add every bone a second time under its other spelling.
      const bool present = std::any_of(other->bones.begin(), other->bones.end(),
                                       [&bone](const PuppetDocumentBone& candidate) {
                                         return candidate.start_joint == bone.start_joint &&
                                                candidate.end_joint == bone.end_joint;
                                       });
      if (!present) {
        commands.push_back(
            puppet_edit::AddBone{.name = absl::StrCat(bone.start_joint, "-", bone.end_joint),
                                 .start_joint = bone.start_joint,
                                 .end_joint = bone.end_joint});
      }
    }
    if (commands.empty()) {
      update.unchanged.push_back(document_name);
      continue;
    }

    PuppetDocument draft = *other;
    bool applied = true;
    for (const puppet_edit::Command& command : commands) {
      if (ApplyPuppetCommand(draft, command).ok()) continue;
      applied = false;
      break;
    }
    if (!applied) {
      update.blocked.push_back(document_name);
      continue;
    }
    RETURN_IF_ERROR(SavePuppetDocument(path, draft));
    update.changed.push_back(document_name);
  }
  return update;
}

absl::Status PuppetEditorWorkspace::CreateDocument(std::string_view name) {
  ASSIGN_OR_RETURN(const std::filesystem::path path, Resolve(name));
  if (std::filesystem::exists(path)) {
    return absl::AlreadyExistsError(absl::StrCat("'", name, "' already exists"));
  }
  RETURN_IF_ERROR(SavePuppetDocument(path, PuppetDocument{}));
  return Open(name);
}

}  // namespace zebes
