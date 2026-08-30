#include "scripts/animation_artwork_run_manifest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

constexpr size_t kMaximumRunManifestBytes = 1024 * 1024;
constexpr int64_t kMaximumAuthoredFrames = 64;

absl::Status RequireExactObject(const nlohmann::json& object,
                                std::initializer_list<std::string_view> fields,
                                std::string_view context) {
  if (!object.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an object"));
  }
  for (const auto& [field, unused] : object.items()) {
    static_cast<void>(unused);
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat(context, " contains unknown field '", field, "'"));
    }
  }
  for (const std::string_view field : fields) {
    if (!object.contains(std::string(field))) {
      return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", field, "'"));
    }
  }
  return absl::OkStatus();
}

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& object, std::string_view field,
                           std::string_view context) {
  const std::string key(field);
  if (!object.contains(key)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", field, "'"));
  }
  try {
    return object.at(key).get<T>();
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " field '", field, "' is invalid: ", error.what()));
  }
}

absl::StatusOr<std::string> ReadBoundedTextFile(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return absl::NotFoundError(
        absl::StrCat("animation run manifest is not a regular file: ", path.string(),
                     error ? absl::StrCat(": ", error.message()) : ""));
  }
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    return absl::NotFoundError(
        absl::StrCat("could not size animation run manifest: ", error.message()));
  }
  if (size == 0 || size > kMaximumRunManifestBytes) {
    return absl::ResourceExhaustedError(
        "animation run manifest must contain between 1 byte and 1 MiB");
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("could not open animation run manifest: ", path.string()));
  }
  std::string text(static_cast<size_t>(size), '\0');
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(text.size())) {
    return absl::DataLossError("could not read the complete animation run manifest");
  }
  return text;
}

absl::StatusOr<AnimationFeasibilityClip> ParseClip(std::string_view value) {
  if (value == AnimationFeasibilityClipId(AnimationFeasibilityClip::kIdleRight)) {
    return AnimationFeasibilityClip::kIdleRight;
  }
  if (value == AnimationFeasibilityClipId(AnimationFeasibilityClip::kLocomotionRight)) {
    return AnimationFeasibilityClip::kLocomotionRight;
  }
  return absl::InvalidArgumentError(
      "animation run manifest clip must be 'idle-right' or 'locomotion-right'");
}

absl::StatusOr<std::vector<int>> ParseTiming(const nlohmann::json& timing, size_t frame_count) {
  if (!timing.is_object() || timing.size() != 1 ||
      (!timing.contains("uniform_frames_per_cycle") && !timing.contains("frames_per_cycle"))) {
    return absl::InvalidArgumentError(
        "animation run manifest timing must contain exactly one of "
        "'uniform_frames_per_cycle' or 'frames_per_cycle'");
  }
  if (timing.contains("uniform_frames_per_cycle")) {
    ASSIGN_OR_RETURN(const int uniform, Required<int>(timing, "uniform_frames_per_cycle",
                                                      "animation run manifest timing"));
    if (uniform <= 0) {
      return absl::InvalidArgumentError(
          "animation run manifest uniform frame timing must be positive");
    }
    return std::vector<int>(frame_count, uniform);
  }

  const nlohmann::json& ordered = timing.at("frames_per_cycle");
  if (!ordered.is_array() || ordered.size() != frame_count) {
    return absl::InvalidArgumentError(
        "animation run manifest ordered timing must match the sheet frame count");
  }
  std::vector<int> result;
  result.reserve(frame_count);
  for (size_t index = 0; index < ordered.size(); ++index) {
    if (!ordered[index].is_number_integer()) {
      return absl::InvalidArgumentError(
          absl::StrCat("animation run manifest frame timing ", index, " must be an integer"));
    }
    try {
      result.push_back(ordered[index].get<int>());
    } catch (const nlohmann::json::exception& error) {
      return absl::InvalidArgumentError(absl::StrCat("animation run manifest frame timing ", index,
                                                     " is invalid: ", error.what()));
    }
    if (result.back() <= 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("animation run manifest frame timing ", index, " must be positive"));
    }
  }
  return result;
}

absl::StatusOr<std::vector<bool>> ParsePlantedFrames(const nlohmann::json& planted,
                                                     size_t frame_count) {
  if (!planted.is_array() || planted.size() != frame_count) {
    return absl::InvalidArgumentError(
        "animation run manifest planted_frames must match the sheet frame count");
  }
  std::vector<bool> result;
  result.reserve(frame_count);
  for (size_t index = 0; index < planted.size(); ++index) {
    if (!planted[index].is_boolean()) {
      return absl::InvalidArgumentError(
          absl::StrCat("animation run manifest planted_frames entry ", index, " must be boolean"));
    }
    result.push_back(planted[index].get<bool>());
  }
  return result;
}

}  // namespace

std::vector<RgbaColor> AnimationArtworkFeasibilityPalette() {
  return {
      {.r = 0x10, .g = 0x13, .b = 0x1C, .a = 255}, {.r = 0x26, .g = 0x32, .b = 0x4A, .a = 255},
      {.r = 0x46, .g = 0x56, .b = 0x6E, .a = 255}, {.r = 0xD8, .g = 0xD6, .b = 0xC9, .a = 255},
      {.r = 0xF0, .g = 0xE3, .b = 0xA1, .a = 255}, {.r = 0xE0, .g = 0xA5, .b = 0x4A, .a = 255},
      {.r = 0xA5, .g = 0x4A, .b = 0x3A, .a = 255}, {.r = 0xCB, .g = 0x55, .b = 0x4A, .a = 255},
      {.r = 0x3E, .g = 0x8C, .b = 0x7A, .a = 255}, {.r = 0x79, .g = 0xC7, .b = 0xA5, .a = 255},
      {.r = 0x71, .g = 0x59, .b = 0x8F, .a = 255}, {.r = 0x4A, .g = 0x2E, .b = 0x42, .a = 255},
  };
}

absl::StatusOr<AnimationArtworkRunManifest> LoadAnimationArtworkRunManifest(
    const std::filesystem::path& path) {
  ASSIGN_OR_RETURN(const std::string text, ReadBoundedTextFile(path));
  const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
  if (document.is_discarded()) {
    return absl::InvalidArgumentError("animation run manifest is not valid JSON");
  }
  RETURN_IF_ERROR(
      RequireExactObject(document, {"schema_version", "clip", "sheet", "timing", "planted_frames"},
                         "animation run manifest"));
  ASSIGN_OR_RETURN(const int schema_version,
                   Required<int>(document, "schema_version", "animation run manifest"));
  if (schema_version != 1) {
    return absl::FailedPreconditionError(absl::StrCat(
        "animation run manifest schema version ", schema_version, " is not supported version 1"));
  }
  ASSIGN_OR_RETURN(const std::string clip_id,
                   Required<std::string>(document, "clip", "animation run manifest"));
  ASSIGN_OR_RETURN(const AnimationFeasibilityClip clip, ParseClip(clip_id));

  const nlohmann::json& sheet_json = document.at("sheet");
  RETURN_IF_ERROR(RequireExactObject(
      sheet_json,
      {"grid_x", "grid_y", "cell_width", "cell_height", "column_gap", "row_gap", "columns", "rows"},
      "animation run manifest sheet"));
  AnimationFeasibilitySheetLayout sheet;
  ASSIGN_OR_RETURN(sheet.grid_x,
                   Required<int>(sheet_json, "grid_x", "animation run manifest sheet"));
  ASSIGN_OR_RETURN(sheet.grid_y,
                   Required<int>(sheet_json, "grid_y", "animation run manifest sheet"));
  ASSIGN_OR_RETURN(sheet.cell_width,
                   Required<int>(sheet_json, "cell_width", "animation run manifest sheet"));
  ASSIGN_OR_RETURN(sheet.cell_height,
                   Required<int>(sheet_json, "cell_height", "animation run manifest sheet"));
  ASSIGN_OR_RETURN(sheet.column_gap,
                   Required<int>(sheet_json, "column_gap", "animation run manifest sheet"));
  ASSIGN_OR_RETURN(sheet.row_gap,
                   Required<int>(sheet_json, "row_gap", "animation run manifest sheet"));
  ASSIGN_OR_RETURN(sheet.columns,
                   Required<int>(sheet_json, "columns", "animation run manifest sheet"));
  ASSIGN_OR_RETURN(sheet.rows, Required<int>(sheet_json, "rows", "animation run manifest sheet"));
  if (sheet.grid_x < 0 || sheet.grid_y < 0 || sheet.cell_width <= 0 || sheet.cell_height <= 0 ||
      sheet.column_gap < 0 || sheet.row_gap < 0 || sheet.columns <= 0 || sheet.rows <= 0) {
    return absl::InvalidArgumentError("animation run manifest sheet layout is invalid");
  }
  const int64_t frame_count = static_cast<int64_t>(sheet.columns) * sheet.rows;
  if (frame_count <= 0 || frame_count > kMaximumAuthoredFrames) {
    return absl::InvalidArgumentError(
        "animation run manifest sheet frame count must be between 1 and 64");
  }

  ASSIGN_OR_RETURN(std::vector<int> timing,
                   ParseTiming(document.at("timing"), static_cast<size_t>(frame_count)));
  ASSIGN_OR_RETURN(std::vector<bool> planted, ParsePlantedFrames(document.at("planted_frames"),
                                                                 static_cast<size_t>(frame_count)));
  return AnimationArtworkRunManifest{
      .clip = clip,
      .sheet = sheet,
      .frames_per_cycle = std::move(timing),
      .planted_frames = std::move(planted),
  };
}

absl::StatusOr<AnimationArtworkFeasibilityConfig> MakeAnimationArtworkRunManifestConfig(
    const AnimationArtworkRunManifest& manifest, std::vector<RgbaColor> palette) {
  return MakeAuthoredAnimationArtworkFeasibilityConfig(
      manifest.clip, manifest.sheet, std::move(palette), manifest.frames_per_cycle,
      manifest.planted_frames);
}

}  // namespace zebes
