#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/atomic_directory_publisher.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "nlohmann/json.hpp"

ABSL_FLAG(std::string, manifest, "",
          "Generated feasibility manifest containing deterministic_payload");
ABSL_FLAG(std::string, output, "", "New disposable staged asset-root directory");
ABSL_FLAG(std::string, state_key, "run-right",
          "One right-facing Player Animation Proof state to bind");

namespace zebes {
namespace {

constexpr char kCheckedInBlueprint[] =
    "definitions/blueprints/Player Animation Proof-1be81945-b011-4342-9109-a10c4040078c.json";
constexpr char kBlueprintId[] = "1be81945-b011-4342-9109-a10c4040078c";
constexpr char kColliderId[] = "2ef185c9-3a26-4729-8373-416e36ed67c7";
constexpr char kTextureId[] = "f4b7cf2a-8a2a-4bcb-9ce8-02b91d75f4aa";
constexpr char kSpriteId[] = "1f2da9b8-9e5c-4fd8-a25a-8f7f5f2f34d6";
constexpr char kTexturePath[] =
    "textures/animation-feasibility/f4b7cf2a-8a2a-4bcb-9ce8-02b91d75f4aa.png";

absl::StatusOr<std::string> ReadText(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open file: ", path.string()));
  }
  std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (stream.bad()) {
    return absl::InternalError(absl::StrCat("could not read file: ", path.string()));
  }
  return contents;
}

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& document) {
  std::ofstream stream(path);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not open JSON output: ", path.string()));
  }
  stream << document.dump(2) << '\n';
  if (!stream.good()) {
    return absl::InternalError(absl::StrCat("could not write JSON output: ", path.string()));
  }
  return absl::OkStatus();
}

absl::Status CopyTree(const std::filesystem::path& source,
                      const std::filesystem::path& destination) {
  std::error_code error;
  if (!std::filesystem::is_directory(source, error) || error) {
    return absl::NotFoundError(
        absl::StrCat("checked-in asset root is not a directory: ", source.string()));
  }
  std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not copy checked-in asset root: ", error.message()));
  }
  return absl::OkStatus();
}

absl::Status CopyFile(const std::filesystem::path& source,
                      const std::filesystem::path& destination) {
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create staged asset directory: ", error.message()));
  }
  if (!std::filesystem::copy_file(source, destination, error)) {
    return absl::InternalError(absl::StrCat("could not install packed texture: ",
                                            error ? error.message() : "source is missing"));
  }
  return absl::OkStatus();
}

absl::Status ValidateOutputOutsideAssets(const std::filesystem::path& output) {
  const std::filesystem::path assets =
      std::filesystem::weakly_canonical(std::filesystem::path(ZEBES_CHECKED_IN_ASSETS_DIR));
  const std::filesystem::path candidate = std::filesystem::weakly_canonical(output);
  const std::filesystem::path relative = candidate.lexically_relative(assets);
  if (!relative.empty() && relative != "." && *relative.begin() != "..") {
    return absl::PermissionDeniedError(
        absl::StrCat("staging output must be outside checked-in assets: ", output.string()));
  }
  return absl::OkStatus();
}

absl::StatusOr<const nlohmann::json*> FindArtifact(const nlohmann::json& payload,
                                                   std::string_view id) {
  if (!payload.contains("artifacts") || !payload.at("artifacts").is_array()) {
    return absl::InvalidArgumentError("deterministic_payload.artifacts must be an array");
  }
  const nlohmann::json* result = nullptr;
  for (size_t index = 0; index < payload.at("artifacts").size(); ++index) {
    const nlohmann::json& artifact = payload.at("artifacts")[index];
    if (!artifact.is_object() || !artifact.contains("id") || !artifact.at("id").is_string()) {
      return absl::InvalidArgumentError(absl::StrCat("deterministic_payload.artifacts[", index,
                                                     "] must be an object with a string id"));
    }
    if (artifact.at("id").get_ref<const std::string&>() != id) continue;
    if (result != nullptr) {
      return absl::InvalidArgumentError(absl::StrCat("manifest artifact id is duplicated: ", id));
    }
    result = &artifact;
  }
  if (result == nullptr) {
    return absl::NotFoundError(absl::StrCat("manifest artifact is missing: ", id));
  }
  return result;
}

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& object, std::string_view field,
                           std::string_view context) {
  const std::string key(field);
  if (!object.is_object() || !object.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", field, "'"));
  }
  try {
    return object.at(key).get<T>();
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " field '", field, "' is invalid: ", error.what()));
  }
}

struct ValidatedAnimationPayload {
  size_t frame_count = 0;
  int output_width = 0;
  int packed_height = 0;
};

absl::StatusOr<ValidatedAnimationPayload> ValidateAnimationPayload(const nlohmann::json& payload,
                                                                   std::string_view expected_clip) {
  if (!payload.contains("contract") || !payload.at("contract").is_object()) {
    return absl::InvalidArgumentError("deterministic_payload.contract must be an object");
  }
  const nlohmann::json& contract = payload.at("contract");
  ASSIGN_OR_RETURN(const std::string clip,
                   Required<std::string>(contract, "clip", "deterministic_payload.contract"));
  if (clip != expected_clip) {
    return absl::InvalidArgumentError(
        "deterministic_payload.contract clip does not match the staged state");
  }
  if (!contract.contains("sheet") || !contract.at("sheet").is_object()) {
    return absl::InvalidArgumentError("deterministic_payload.contract.sheet must be an object");
  }
  const nlohmann::json& sheet = contract.at("sheet");
  ASSIGN_OR_RETURN(const int columns,
                   Required<int>(sheet, "columns", "deterministic_payload.contract.sheet"));
  ASSIGN_OR_RETURN(const int rows,
                   Required<int>(sheet, "rows", "deterministic_payload.contract.sheet"));
  const int64_t frame_count = static_cast<int64_t>(columns) * rows;
  if (frame_count <= 0 || frame_count > 64) {
    return absl::InvalidArgumentError(
        "deterministic_payload contract frame count must be between 1 and 64");
  }
  if (contract.contains("frame_count")) {
    ASSIGN_OR_RETURN(const int declared_frame_count,
                     Required<int>(contract, "frame_count", "deterministic_payload.contract"));
    if (declared_frame_count != frame_count) {
      return absl::InvalidArgumentError(
          "deterministic_payload.contract frame_count does not match its sheet");
    }
  }
  const size_t count = static_cast<size_t>(frame_count);
  if (!contract.contains("frames_per_cycle") || !contract.at("frames_per_cycle").is_array() ||
      contract.at("frames_per_cycle").size() != count) {
    return absl::InvalidArgumentError(
        "deterministic_payload.contract frames_per_cycle must match its sheet frame count");
  }
  const nlohmann::json& timing = contract.at("frames_per_cycle");
  for (size_t index = 0; index < count; ++index) {
    if (!timing[index].is_number_integer()) {
      return absl::InvalidArgumentError(
          absl::StrCat("deterministic payload frame timing ", index, " must be an integer"));
    }
    try {
      if (timing[index].get<int>() <= 0) {
        return absl::InvalidArgumentError(
            absl::StrCat("deterministic payload frame timing ", index, " must be positive"));
      }
    } catch (const nlohmann::json::exception& error) {
      return absl::InvalidArgumentError(absl::StrCat("deterministic payload frame timing ", index,
                                                     " is invalid: ", error.what()));
    }
  }
  if (!contract.contains("planted_frames") || !contract.at("planted_frames").is_array() ||
      contract.at("planted_frames").size() != count) {
    return absl::InvalidArgumentError(
        "deterministic_payload.contract planted_frames must match its sheet frame count");
  }
  const nlohmann::json& planted = contract.at("planted_frames");
  for (size_t index = 0; index < count; ++index) {
    if (!planted[index].is_boolean()) {
      return absl::InvalidArgumentError(
          absl::StrCat("deterministic payload planted_frames entry ", index, " must be boolean"));
    }
  }

  ASSIGN_OR_RETURN(const int output_width,
                   Required<int>(contract, "output_width", "deterministic_payload.contract"));
  ASSIGN_OR_RETURN(const int packed_height,
                   Required<int>(contract, "contact_line_y", "deterministic_payload.contract"));
  if (output_width <= 0 || packed_height <= 0 ||
      frame_count > std::numeric_limits<int>::max() / output_width) {
    return absl::InvalidArgumentError("deterministic_payload contract packed geometry is invalid");
  }

  if (!payload.contains("sprite_frames") || !payload.at("sprite_frames").is_array() ||
      payload.at("sprite_frames").size() != count) {
    return absl::InvalidArgumentError(
        "deterministic_payload.sprite_frames must match the contract frame count");
  }
  const nlohmann::json& sprite_frames = payload.at("sprite_frames");
  for (size_t index = 0; index < count; ++index) {
    const std::string context = absl::StrCat("deterministic_payload.sprite_frames[", index, "]");
    ASSIGN_OR_RETURN(const int frame_index, Required<int>(sprite_frames[index], "index", context));
    ASSIGN_OR_RETURN(const int texture_x,
                     Required<int>(sprite_frames[index], "texture_x", context));
    ASSIGN_OR_RETURN(const int texture_y,
                     Required<int>(sprite_frames[index], "texture_y", context));
    ASSIGN_OR_RETURN(const int texture_w,
                     Required<int>(sprite_frames[index], "texture_w", context));
    ASSIGN_OR_RETURN(const int texture_h,
                     Required<int>(sprite_frames[index], "texture_h", context));
    ASSIGN_OR_RETURN(const int frame_timing,
                     Required<int>(sprite_frames[index], "frames_per_cycle", context));
    if (frame_index != static_cast<int>(index) ||
        texture_x != static_cast<int>(index) * output_width || texture_y != 0 ||
        texture_w != output_width || texture_h != packed_height ||
        frame_timing != timing[index].get<int>()) {
      return absl::InvalidArgumentError(
          absl::StrCat(context, " does not match the authored contract"));
    }
  }

  if (!payload.contains("diagnostics") || !payload.at("diagnostics").is_object() ||
      !payload.at("diagnostics").contains("frames") ||
      !payload.at("diagnostics").at("frames").is_array() ||
      payload.at("diagnostics").at("frames").size() != count) {
    return absl::InvalidArgumentError(
        "deterministic_payload diagnostics frames must match the contract frame count");
  }
  const nlohmann::json& diagnostics = payload.at("diagnostics").at("frames");
  for (size_t index = 0; index < count; ++index) {
    const std::string context =
        absl::StrCat("deterministic_payload.diagnostics.frames[", index, "]");
    ASSIGN_OR_RETURN(const int diagnostic_index,
                     Required<int>(diagnostics[index], "index", context));
    ASSIGN_OR_RETURN(const bool contact_line_hit,
                     Required<bool>(diagnostics[index], "contact_line_hit", context));
    if (diagnostic_index != static_cast<int>(index)) {
      return absl::InvalidArgumentError(absl::StrCat(context, " has the wrong index"));
    }
    if (planted[index].get<bool>() && !contact_line_hit) {
      return absl::FailedPreconditionError(
          absl::StrCat(context, " violates its planted-foot contact expectation"));
    }
  }
  return ValidatedAnimationPayload{
      .frame_count = count,
      .output_width = output_width,
      .packed_height = packed_height,
  };
}

absl::Status Stage(const std::filesystem::path& manifest_path, const std::filesystem::path& output,
                   std::string_view state_key) {
  if (state_key != "idle-right" && state_key != "run-right") {
    return absl::InvalidArgumentError("--state_key must be idle-right or run-right");
  }
  const std::string_view expected_clip =
      state_key == "idle-right" ? "idle-right" : "locomotion-right";
  RETURN_IF_ERROR(ValidateNewDirectoryDestination(output.string()));
  RETURN_IF_ERROR(ValidateOutputOutsideAssets(output));

  ASSIGN_OR_RETURN(const std::string manifest_text, ReadText(manifest_path));
  nlohmann::json source_manifest;
  try {
    source_manifest = nlohmann::json::parse(manifest_text);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not parse feasibility manifest: ", error.what()));
  }
  if (!source_manifest.contains("deterministic_payload") ||
      !source_manifest.at("deterministic_payload").is_object()) {
    return absl::InvalidArgumentError("manifest must contain an object deterministic_payload");
  }
  if (source_manifest.value("bundle", "") != "animation-artwork-feasibility" ||
      source_manifest.value("clip", "") != expected_clip) {
    return absl::InvalidArgumentError(absl::StrCat("staging state ", state_key,
                                                   " requires an animation-artwork-feasibility ",
                                                   expected_clip, " manifest"));
  }
  const nlohmann::json& payload = source_manifest.at("deterministic_payload");
  if (!payload.contains("hard_validation") || !payload.at("hard_validation").is_object()) {
    return absl::InvalidArgumentError("deterministic_payload.hard_validation must be an object");
  }
  ASSIGN_OR_RETURN(const bool hard_validation_passed,
                   Required<bool>(payload.at("hard_validation"), "passed",
                                  "deterministic_payload.hard_validation"));
  if (!hard_validation_passed) {
    return absl::FailedPreconditionError(
        "staging requires deterministic_payload.hard_validation.passed=true");
  }
  ASSIGN_OR_RETURN(const ValidatedAnimationPayload validated,
                   ValidateAnimationPayload(payload, expected_clip));
  ASSIGN_OR_RETURN(const nlohmann::json* packed, FindArtifact(payload, "packed-texture"));
  ASSIGN_OR_RETURN(const int packed_width,
                   Required<int>(*packed, "width", "packed-texture artifact"));
  ASSIGN_OR_RETURN(const int packed_height,
                   Required<int>(*packed, "height", "packed-texture artifact"));
  if (packed_width != static_cast<int>(validated.frame_count) * validated.output_width ||
      packed_height != validated.packed_height) {
    return absl::InvalidArgumentError(
        "packed-texture artifact dimensions do not match the authored contract");
  }
  ASSIGN_OR_RETURN(const std::string packed_path,
                   Required<std::string>(*packed, "path", "packed-texture artifact"));
  if (packed_path.empty() || std::filesystem::path(packed_path).is_absolute() ||
      std::filesystem::path(packed_path).lexically_normal().string().starts_with("..")) {
    return absl::InvalidArgumentError("packed-texture path must be relative to the manifest");
  }
  const std::filesystem::path packed_source = manifest_path.parent_path() / packed_path;
  std::error_code error;
  if (!std::filesystem::is_regular_file(packed_source, error) || error) {
    return absl::NotFoundError(absl::StrCat("packed texture is missing: ", packed_source.string()));
  }

  const std::filesystem::path checked_in_root(ZEBES_CHECKED_IN_ASSETS_DIR);
  const std::filesystem::path source_blueprint = checked_in_root / kCheckedInBlueprint;
  ASSIGN_OR_RETURN(const std::string original_blueprint, ReadText(source_blueprint));
  nlohmann::json blueprint;
  try {
    blueprint = nlohmann::json::parse(original_blueprint);
  } catch (const nlohmann::json::exception& parse_error) {
    return absl::InternalError(
        absl::StrCat("checked-in Player Animation Proof is invalid: ", parse_error.what()));
  }
  if (blueprint.value("id", "") != kBlueprintId || !blueprint.contains("states") ||
      !blueprint.at("states").is_array()) {
    return absl::FailedPreconditionError("unexpected Player Animation Proof blueprint identity");
  }
  nlohmann::json original_target;
  bool found_target = false;
  std::map<std::string, nlohmann::json> original_states;
  for (const auto& state : blueprint.at("states")) {
    const std::string key = state.value("key", "");
    if (key.empty() || original_states.contains(key)) {
      return absl::FailedPreconditionError("Player Animation Proof state keys are not unique");
    }
    original_states.emplace(key, state);
    if (key == state_key) {
      original_target = state;
      found_target = true;
      if (state.value("collider_id", "") != kColliderId ||
          state.value("placement_mode", "") != "grounded") {
        return absl::FailedPreconditionError("target state has unexpected collider placement");
      }
    }
  }
  if (!found_target) {
    return absl::NotFoundError(absl::StrCat("blueprint state is missing: ", state_key));
  }

  const nlohmann::json& sprite_frames = payload.at("sprite_frames");
  const nlohmann::json texture_definition{
      {"id", kTextureId},
      {"name", absl::StrCat("Animation Feasibility ", expected_clip, " Texture")},
      {"path", kTexturePath}};
  const nlohmann::json sprite_definition{
      {"frames", sprite_frames},
      {"id", kSpriteId},
      {"name", absl::StrCat("Animation Feasibility ", expected_clip)},
      {"playback_mode", "loop"},
      {"texture_id", kTextureId}};

  return PublishNewDirectoryAtomically(output.string(), [&](const std::filesystem::path& staging) {
    const std::filesystem::path staged_assets = staging / "assets";
    RETURN_IF_ERROR(CopyTree(checked_in_root, staged_assets));
    RETURN_IF_ERROR(CopyFile(packed_source, staged_assets / kTexturePath));
    RETURN_IF_ERROR(
        WriteJson(staged_assets / "definitions/textures/animation-feasibility-live-proof.json",
                  texture_definition));
    RETURN_IF_ERROR(
        WriteJson(staged_assets / "definitions/sprites/animation-feasibility-live-proof.json",
                  sprite_definition));

    nlohmann::json staged_blueprint = blueprint;
    for (auto& state : staged_blueprint.at("states")) {
      if (state.at("key").get<std::string>() == state_key) state["sprite_id"] = kSpriteId;
    }
    for (const auto& state : staged_blueprint.at("states")) {
      const std::string key = state.at("key").get<std::string>();
      if (key != state_key && state != original_states.at(key)) {
        return absl::InternalError(absl::StrCat("unrelated blueprint state changed: ", key));
      }
      if (key == state_key) {
        nlohmann::json expected = original_target;
        expected["sprite_id"] = kSpriteId;
        if (state != expected)
          return absl::InternalError("target blueprint state changed unexpectedly");
      }
    }
    RETURN_IF_ERROR(WriteJson(staged_assets / kCheckedInBlueprint, staged_blueprint));

    nlohmann::json staged_payload = payload;
    staged_payload["staging"] = {
        {"asset_root", "assets"},
        {"blueprint_id", kBlueprintId},
        {"collider_id_asserted", kColliderId},
        {"state_key", state_key},
        {"texture_id", kTextureId},
        {"sprite_id", kSpriteId},
        {"unrelated_states_unchanged", true},
    };
    nlohmann::json staged_manifest{
        {"bundle", "animation-artwork-live-proof-staging"},
        {"source_manifest", std::filesystem::absolute(manifest_path).string()},
        {"created_at_utc", CurrentUtcTimestamp()},
        {"deterministic_payload", staged_payload},
    };
    RETURN_IF_ERROR(WriteJson(staging / "manifest.json", staged_manifest));
    RETURN_IF_ERROR(
        WriteJson(staging / "runtime.json", {{"asset_root", "assets"}, {"state_key", state_key}}));
    return absl::OkStatus();
  });
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::InitializeLog();
  absl::ParseCommandLine(argc, argv);
  const std::string manifest = absl::GetFlag(FLAGS_manifest);
  const std::string output = absl::GetFlag(FLAGS_output);
  if (manifest.empty() || output.empty()) {
    LOG(ERROR) << "--manifest and --output must be non-empty";
    return 2;
  }
  const absl::Status status = zebes::Stage(manifest, output, absl::GetFlag(FLAGS_state_key));
  if (!status.ok()) {
    LOG(ERROR) << status;
    return 1;
  }
  LOG(INFO) << "published disposable live-proof asset root: "
            << (std::filesystem::path(output) / "assets");
  return 0;
}
