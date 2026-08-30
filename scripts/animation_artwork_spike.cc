// Milestone-2 feasibility spike for coherent animation artwork sheets.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "artwork/animation_artwork_feasibility.h"
#include "common/atomic_directory_publisher.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "nlohmann/json.hpp"
#include "scripts/animation_artwork_run_manifest.h"

ABSL_FLAG(std::string, input, "", "Input coherent animation-sheet PNG");
ABSL_FLAG(std::string, clip, "", "Animation clip: idle or locomotion");
ABSL_FLAG(std::string, run_manifest, "",
          "Optional schema-v1 authored run contract; replaces clip and grid flags");
ABSL_FLAG(std::string, output, "", "New directory in which to publish feasibility evidence");
ABSL_FLAG(int, cell_size, 0, "Square source-cell size in pixels");
ABSL_FLAG(int, grid_x, 0, "Source-sheet grid origin X in pixels");
ABSL_FLAG(int, grid_y, 0, "Source-sheet grid origin Y in pixels");
ABSL_FLAG(int, column_gap, 0, "Gap between source columns in pixels");
ABSL_FLAG(int, row_gap, 0, "Gap between source rows in pixels");
ABSL_FLAG(std::string, provider, "", "Optional source provider label");
ABSL_FLAG(std::string, model, "", "Optional source model label");
ABSL_FLAG(std::string, submitted_prompt, "", "Optional exact submitted prompt");
ABSL_FLAG(std::string, revised_prompt, "", "Optional provider-revised prompt");
ABSL_FLAG(std::string, source_label, "", "Optional anonymous source label");

namespace zebes {
namespace {

constexpr size_t kMaximumEncodedBytes = 64 * 1024 * 1024;
constexpr int kColumnsIdle = 2;
constexpr int kRowsIdle = 2;
constexpr int kColumnsLocomotion = 5;
constexpr int kRowsLocomotion = 2;

nlohmann::json ColorToJson(const RgbaColor& color) {
  return {{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
}

nlohmann::json BoundsToJson(const AnimationFeasibilityBounds& bounds) {
  return {
      {"left", bounds.left},
      {"top", bounds.top},
      {"right", bounds.right},
      {"bottom", bounds.bottom},
  };
}

nlohmann::json FrameToJson(const SpriteFrame& frame) {
  return {
      {"index", frame.index},         {"texture_x", frame.texture_x},
      {"texture_y", frame.texture_y}, {"texture_w", frame.texture_w},
      {"texture_h", frame.texture_h}, {"render_w", frame.render_w},
      {"render_h", frame.render_h},   {"frames_per_cycle", frame.frames_per_cycle},
      {"offset_x", frame.offset_x},   {"offset_y", frame.offset_y},
  };
}

absl::StatusOr<std::vector<uint8_t>> ReadBoundedFile(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    if (error) {
      return absl::NotFoundError(absl::StrCat("could not inspect input PNG: ", error.message()));
    }
    return absl::NotFoundError(absl::StrCat("input PNG is not a regular file: ", path.string()));
  }
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    return absl::NotFoundError(absl::StrCat("could not size input PNG: ", error.message()));
  }
  if (size > kMaximumEncodedBytes) {
    return absl::ResourceExhaustedError("input PNG exceeds the 64 MiB encoded byte limit");
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open input PNG: ", path.string()));
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
      return absl::DataLossError(absl::StrCat("could not read input PNG: ", path.string()));
    }
  }
  return bytes;
}

absl::StatusOr<AnimationFeasibilityClip> ParseClip(std::string_view value) {
  if (value == "idle") return AnimationFeasibilityClip::kIdleRight;
  if (value == "locomotion") return AnimationFeasibilityClip::kLocomotionRight;
  return absl::InvalidArgumentError("--clip must be idle or locomotion");
}

bool IsDescendant(const std::filesystem::path& path, const std::filesystem::path& root) {
  const std::filesystem::path relative = path.lexically_relative(root);
  if (relative.empty() || relative.is_absolute()) return false;
  return relative != "." && *relative.begin() != "..";
}

absl::Status ValidateOutputOutsideAssets(const std::string& output) {
  RETURN_IF_ERROR(ValidateNewDirectoryDestination(output));

  std::error_code error;
  const std::filesystem::path canonical_output =
      std::filesystem::weakly_canonical(std::filesystem::path(output), error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not resolve output directory: ", error.message()));
  }

#ifdef ZEBES_CHECKED_IN_ASSETS_DIR
  const std::filesystem::path assets_root =
      std::filesystem::weakly_canonical(ZEBES_CHECKED_IN_ASSETS_DIR, error);
  if (!error && IsDescendant(canonical_output, assets_root)) {
    return absl::InvalidArgumentError(
        "animation feasibility output must not be inside the checked-in assets directory");
  }
#endif
  return absl::OkStatus();
}

RgbaImage ScaleNearest(const RgbaImage& source, int scale) {
  RgbaImage output{
      .width = source.width * scale,
      .height = source.height * scale,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(source.width * scale) * source.height *
                                     scale * 4),
  };
  for (int y = 0; y < output.height; ++y) {
    for (int x = 0; x < output.width; ++x) {
      const size_t source_offset = (static_cast<size_t>(y / scale) * source.width + x / scale) * 4;
      const size_t output_offset = (static_cast<size_t>(y) * output.width + x) * 4;
      std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(source_offset), 4,
                  output.pixels.begin() + static_cast<ptrdiff_t>(output_offset));
    }
  }
  return output;
}

RgbaImage BuildContactSheet(const std::vector<AnimationFeasibilityFrameResult>& frames,
                            const AnimationArtworkFeasibilityConfig& config, int scale) {
  const int cell_width = config.output_width * scale;
  const int cell_height = config.output_height * scale;
  RgbaImage output{
      .width = config.sheet.columns * cell_width,
      .height = config.sheet.rows * cell_height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(config.sheet.columns * cell_width) *
                                     config.sheet.rows * cell_height * 4),
  };
  for (size_t index = 0; index < frames.size(); ++index) {
    const RgbaImage enlarged = ScaleNearest(frames[index].finished, scale);
    const int destination_x = static_cast<int>(index % config.sheet.columns) * cell_width;
    const int destination_y = static_cast<int>(index / config.sheet.columns) * cell_height;
    for (int y = 0; y < cell_height; ++y) {
      const size_t source_offset = static_cast<size_t>(y) * cell_width * 4;
      const size_t destination_offset =
          (static_cast<size_t>(destination_y + y) * output.width + destination_x) * 4;
      std::copy_n(enlarged.pixels.begin() + static_cast<ptrdiff_t>(source_offset),
                  static_cast<size_t>(cell_width) * 4,
                  output.pixels.begin() + static_cast<ptrdiff_t>(destination_offset));
    }
  }
  return output;
}

void SetPixel(RgbaImage& image, int x, int y, RgbaColor color) {
  if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
  const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
  image.pixels[offset + 0] = color.r;
  image.pixels[offset + 1] = color.g;
  image.pixels[offset + 2] = color.b;
  image.pixels[offset + 3] = color.a;
}

void DrawHorizontal(RgbaImage& image, int left, int right, int y, RgbaColor color) {
  for (int x = left; x <= right; ++x) SetPixel(image, x, y, color);
}

void DrawVertical(RgbaImage& image, int top, int bottom, int x, RgbaColor color) {
  for (int y = top; y <= bottom; ++y) SetPixel(image, x, y, color);
}

RgbaImage Overlay(const RgbaImage& frame, const AnimationFeasibilityFrameDiagnostics& diagnostics,
                  const AnimationArtworkFeasibilityConfig& config) {
  RgbaImage output = frame;
  const RgbaColor origin{.r = 80, .g = 220, .b = 255, .a = 255};
  const RgbaColor contact{.r = 255, .g = 220, .b = 40, .a = 255};
  const RgbaColor bounds{.r = 70, .g = 230, .b = 110, .a = 255};
  const RgbaColor clipping{.r = 255, .g = 70, .b = 70, .a = 255};
  DrawHorizontal(output, 0, output.width - 1, config.contact_line_y, contact);
  DrawVertical(output, 0, output.height - 1, config.origin_x, origin);
  DrawHorizontal(output, 0, output.width - 1, config.origin_y, origin);
  DrawHorizontal(output, diagnostics.bounds.left, diagnostics.bounds.right - 1,
                 diagnostics.bounds.top, bounds);
  DrawHorizontal(output, diagnostics.bounds.left, diagnostics.bounds.right - 1,
                 diagnostics.bounds.bottom - 1, bounds);
  DrawVertical(output, diagnostics.bounds.top, diagnostics.bounds.bottom - 1,
               diagnostics.bounds.left, bounds);
  DrawVertical(output, diagnostics.bounds.top, diagnostics.bounds.bottom - 1,
               diagnostics.bounds.right - 1, bounds);
  DrawHorizontal(output, 0, output.width - 1, 0, clipping);
  DrawHorizontal(output, 0, output.width - 1, output.height - 1, clipping);
  DrawVertical(output, 0, output.height - 1, 0, clipping);
  DrawVertical(output, 0, output.height - 1, output.width - 1, clipping);
  return output;
}

nlohmann::json ContractToJson(const AnimationArtworkFeasibilityConfig& config) {
  nlohmann::json timing = nlohmann::json::array();
  for (const int ticks : config.frames_per_cycle) timing.push_back(ticks);
  nlohmann::json planted = nlohmann::json::array();
  for (const bool value : config.planted_frames) planted.push_back(value);
  nlohmann::json palette = nlohmann::json::array();
  for (const RgbaColor& color : config.palette) palette.push_back(ColorToJson(color));
  return {
      {"clip", AnimationFeasibilityClipId(config.clip)},
      {"frame_count", config.frames_per_cycle.size()},
      {"output_width", config.output_width},
      {"output_height", config.output_height},
      {"origin", {{"x", config.origin_x}, {"y", config.origin_y}}},
      {"contact_line_y", config.contact_line_y},
      {"render_scale", config.render_scale},
      {"contact_tolerance", config.contact_tolerance},
      {"alpha_threshold", config.alpha_threshold},
      {"minimum_visible_pixels", config.minimum_visible_pixels},
      {"matte", ColorToJson(config.matte)},
      {"palette", std::move(palette)},
      {"frames_per_cycle", std::move(timing)},
      {"planted_frames", std::move(planted)},
      {"sheet",
       {{"grid_x", config.sheet.grid_x},
        {"grid_y", config.sheet.grid_y},
        {"cell_width", config.sheet.cell_width},
        {"cell_height", config.sheet.cell_height},
        {"column_gap", config.sheet.column_gap},
        {"row_gap", config.sheet.row_gap},
        {"columns", config.sheet.columns},
        {"rows", config.sheet.rows}}},
  };
}

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& document) {
  std::ofstream stream(path);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not open manifest: ", path.string()));
  }
  stream << document.dump(2) << '\n';
  if (!stream.good()) {
    return absl::InternalError(absl::StrCat("could not write manifest: ", path.string()));
  }
  return absl::OkStatus();
}

absl::Status Run() {
  const std::string input_path = absl::GetFlag(FLAGS_input);
  const std::string clip_name = absl::GetFlag(FLAGS_clip);
  const std::string run_manifest_path = absl::GetFlag(FLAGS_run_manifest);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  const int cell_size = absl::GetFlag(FLAGS_cell_size);
  if (input_path.empty() || output_path.empty()) {
    return absl::InvalidArgumentError("--input and --output are required");
  }
  RETURN_IF_ERROR(ValidateOutputOutsideAssets(output_path));
  const int grid_x = absl::GetFlag(FLAGS_grid_x);
  const int grid_y = absl::GetFlag(FLAGS_grid_y);
  const int column_gap = absl::GetFlag(FLAGS_column_gap);
  const int row_gap = absl::GetFlag(FLAGS_row_gap);

  AnimationArtworkFeasibilityConfig config;
  if (!run_manifest_path.empty()) {
    if (!clip_name.empty() || cell_size != 0 || grid_x != 0 || grid_y != 0 || column_gap != 0 ||
        row_gap != 0) {
      return absl::InvalidArgumentError(
          "--run_manifest cannot be combined with --clip or sheet/grid flags");
    }
    ASSIGN_OR_RETURN(const AnimationArtworkRunManifest run_manifest,
                     LoadAnimationArtworkRunManifest(run_manifest_path));
    ASSIGN_OR_RETURN(config, MakeAnimationArtworkRunManifestConfig(
                                 run_manifest, AnimationArtworkFeasibilityPalette()));
  } else {
    if (clip_name.empty() || cell_size <= 0) {
      return absl::InvalidArgumentError(
          "set --run_manifest, or set --clip and a positive --cell_size");
    }
    if (grid_x < 0 || grid_y < 0 || column_gap < 0 || row_gap < 0) {
      return absl::InvalidArgumentError("grid origins and gaps must be non-negative");
    }
    ASSIGN_OR_RETURN(const AnimationFeasibilityClip clip, ParseClip(clip_name));
    const int columns =
        clip == AnimationFeasibilityClip::kIdleRight ? kColumnsIdle : kColumnsLocomotion;
    const int rows = clip == AnimationFeasibilityClip::kIdleRight ? kRowsIdle : kRowsLocomotion;
    const AnimationFeasibilitySheetLayout sheet{
        .grid_x = grid_x,
        .grid_y = grid_y,
        .cell_width = cell_size,
        .cell_height = cell_size,
        .column_gap = column_gap,
        .row_gap = row_gap,
        .columns = columns,
        .rows = rows,
    };
    ASSIGN_OR_RETURN(config, MakeAnimationArtworkFeasibilityConfig(
                                 clip, sheet, AnimationArtworkFeasibilityPalette()));
  }
  ASSIGN_OR_RETURN(const std::vector<uint8_t> encoded, ReadBoundedFile(input_path));
  ASSIGN_OR_RETURN(const RgbaImage source,
                   DecodeImage(encoded, static_cast<int64_t>(config.maximum_source_pixels)));
  if (source.width > config.maximum_source_width || source.height > config.maximum_source_height) {
    return absl::ResourceExhaustedError("input PNG dimensions exceed the 2048-pixel limit");
  }
  ASSIGN_OR_RETURN(const AnimationArtworkFeasibilityResult result,
                   RunAnimationArtworkFeasibility(source, config));

  const std::string generated_at_utc = CurrentUtcTimestamp();
  const nlohmann::json provenance{
      {"provider", absl::GetFlag(FLAGS_provider)},
      {"model", absl::GetFlag(FLAGS_model)},
      {"submitted_prompt", absl::GetFlag(FLAGS_submitted_prompt)},
      {"revised_prompt", absl::GetFlag(FLAGS_revised_prompt)},
      {"source_label", absl::GetFlag(FLAGS_source_label)},
      {"run_manifest_filename",
       run_manifest_path.empty()
           ? nlohmann::json(nullptr)
           : nlohmann::json(std::filesystem::path(run_manifest_path).filename().string())},
      {"input_filename", std::filesystem::path(input_path).filename().string()},
      {"source_rgba_sha256", result.source_digest},
      {"source_width", source.width},
      {"source_height", source.height},
      {"encoded_bytes", encoded.size()},
  };

  RETURN_IF_ERROR(PublishNewDirectoryAtomically(
      output_path, [&](const std::filesystem::path& staging) -> absl::Status {
        nlohmann::json artifacts = nlohmann::json::array();
        const auto write_artifact =
            [&](std::string_view id, const std::string& relative_path, const RgbaImage& image,
                nlohmann::json metadata = nlohmann::json::object()) -> absl::Status {
          const std::filesystem::path path = staging / relative_path;
          RETURN_IF_ERROR(WritePng(path.string(), image.width, image.height, image.pixels));
          ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(image));
          artifacts.push_back({{"id", id},
                               {"path", relative_path},
                               {"width", image.width},
                               {"height", image.height},
                               {"rgba_sha256", digest},
                               {"metadata", std::move(metadata)}});
          return absl::OkStatus();
        };

        RETURN_IF_ERROR(write_artifact("source-sheet", "source-sheet.png", source));
        for (size_t index = 0; index < result.frames.size(); ++index) {
          const AnimationFeasibilityFrameResult& frame = result.frames[index];
          const std::string prefix = absl::StrCat("frames/frame-", index);
          const nlohmann::json metadata = {
              {"index", index},
              {"bounds", BoundsToJson(frame.diagnostics.bounds)},
              {"visible_pixels", frame.diagnostics.visible_pixels},
              {"contact_line_hit", frame.diagnostics.contact_line_hit},
          };
          RETURN_IF_ERROR(write_artifact(absl::StrCat("frame-", index, "-extracted"),
                                         absl::StrCat(prefix, "-extracted.png"), frame.extracted,
                                         metadata));
          RETURN_IF_ERROR(write_artifact(absl::StrCat("frame-", index, "-isolated"),
                                         absl::StrCat(prefix, "-isolated.png"), frame.isolated,
                                         metadata));
          RETURN_IF_ERROR(write_artifact(absl::StrCat("frame-", index, "-resized"),
                                         absl::StrCat(prefix, "-resized.png"), frame.resized,
                                         metadata));
          RETURN_IF_ERROR(write_artifact(absl::StrCat("frame-", index, "-native"),
                                         absl::StrCat(prefix, "-native.png"), frame.finished,
                                         metadata));
          RETURN_IF_ERROR(write_artifact(absl::StrCat("frame-", index, "-enlarged"),
                                         absl::StrCat(prefix, "-enlarged.png"),
                                         ScaleNearest(frame.finished, 8), metadata));
          RETURN_IF_ERROR(write_artifact(
              absl::StrCat("frame-", index, "-overlay"), absl::StrCat(prefix, "-overlay.png"),
              Overlay(frame.finished, frame.diagnostics, config), metadata));
        }
        nlohmann::json differences = nlohmann::json::array();
        for (const AnimationFeasibilityFrameDifference& difference : result.differences) {
          const std::string relative_path = absl::StrCat(
              "differences/frame-", difference.from_index, "-to-", difference.to_index, ".png");
          RETURN_IF_ERROR(write_artifact(
              absl::StrCat("difference-", difference.from_index, "-to-", difference.to_index),
              relative_path, difference.image,
              {{"from_index", difference.from_index},
               {"to_index", difference.to_index},
               {"changed_pixels", difference.changed_pixels},
               {"maximum_channel_difference", difference.maximum_channel_difference}}));
          differences.push_back(
              {{"from_index", difference.from_index},
               {"to_index", difference.to_index},
               {"changed_pixels", difference.changed_pixels},
               {"maximum_channel_difference", difference.maximum_channel_difference}});
        }
        RETURN_IF_ERROR(write_artifact("native-contact-sheet-8x", "native-contact-sheet-8x.png",
                                       BuildContactSheet(result.frames, config, 8)));
        RETURN_IF_ERROR(write_artifact("packed-texture", "packed-texture.png",
                                       result.packed_texture,
                                       {{"rgba_sha256", result.packed_digest}}));

        nlohmann::json frame_diagnostics = nlohmann::json::array();
        for (const AnimationFeasibilityFrameResult& frame : result.frames) {
          frame_diagnostics.push_back({
              {"index", frame.diagnostics.index},
              {"bounds", BoundsToJson(frame.diagnostics.bounds)},
              {"visible_pixels", frame.diagnostics.visible_pixels},
              {"contact_line_hit", frame.diagnostics.contact_line_hit},
          });
        }
        nlohmann::json sprite_frames = nlohmann::json::array();
        for (const SpriteFrame& frame : result.sprite_frames) {
          sprite_frames.push_back(FrameToJson(frame));
        }
        const nlohmann::json manifest{
            {"schema_version", 1},
            {"bundle", "animation-artwork-feasibility"},
            {"clip", AnimationFeasibilityClipId(config.clip)},
            {"run", {{"created_at_utc", generated_at_utc}}},
            {"deterministic_payload",
             {{"contract", ContractToJson(config)},
              {"provenance", provenance},
              {"artifacts", std::move(artifacts)},
              {"sprite_frames", std::move(sprite_frames)},
              {"diagnostics",
               {{"frames", std::move(frame_diagnostics)},
                {"differences", std::move(differences)},
                {"packed_rgba_sha256", result.packed_digest}}},
              {"hard_validation", {{"passed", true}, {"errors", nlohmann::json::array()}}}}},
        };
        return WriteJson(staging / "manifest.json", manifest);
      }));

  LOG(INFO) << "Published animation feasibility " << AnimationFeasibilityClipId(config.clip)
            << " evidence at " << output_path;
  std::cout << (std::filesystem::path(output_path) / "manifest.json").string() << '\n';
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << "Animation artwork feasibility failed: " << status;
    return 1;
  }
  return 0;
}
