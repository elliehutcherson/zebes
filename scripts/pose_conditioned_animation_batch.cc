#include "scripts/pose_conditioned_animation_batch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "artwork/animation_artwork_feasibility.h"
#include "common/atomic_directory_publisher.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "common/utils.h"
#include "generation/headless_asset_generation.h"
#include "generation/image_generation.h"
#include "generation/image_generation_engine.h"
#include "generation/image_generation_service.h"
#include "nlohmann/json.hpp"
#include "scripts/animation_artwork_run_manifest.h"

namespace zebes {
namespace {

constexpr size_t kMaximumExperimentManifestBytes = 1024 * 1024;
constexpr size_t kMaximumApprovalBytes = 1024 * 1024;
constexpr size_t kMaximumPilotManifestBytes = 16 * 1024 * 1024;
constexpr int64_t kMaximumReferencePixels = 16 * 1024 * 1024;
constexpr int64_t kMaximumBatchOutputPixels = 64 * 1024 * 1024;
constexpr size_t kFrameCount = 12;
constexpr int kColumns = 6;
constexpr int kRows = 2;
constexpr int kPilotFirstFrame = 0;
constexpr int kPilotSecondFrame = 6;
constexpr absl::Duration kGenerationTimeout = absl::Minutes(10);

struct LockedExperiment {
  std::filesystem::path manifest_path;
  std::filesystem::path animation_run_manifest_path;
  std::string experiment_id;
  HeadlessImageReferenceSource identity_source;
  HeadlessImageReferenceSource pose_sheet_source;
  AnimationFeasibilitySheetLayout pose_sheet_layout;
  std::string provider;
  std::string model;
  std::string instructions;
  std::string prompt;
  std::string negative_requirements;
  ImageAspectRatio target_aspect;
  ImageTransparencyPreference transparency = ImageTransparencyPreference::kNoPreference;
  int expected_output_width = 0;
  int expected_output_height = 0;
};

struct LockedInputs {
  AnimationArtworkRunManifest animation;
  ResolvedHeadlessImageReference identity;
  ResolvedHeadlessImageReference pose_sheet;
  std::vector<RgbaImage> poses;
  std::vector<std::string> pose_digests;
  nlohmann::json request_shape;
};

struct PilotApproval {
  std::filesystem::path approval_path;
  std::filesystem::path pilot_manifest_path;
  std::string pilot_run_id;
  nlohmann::json evidence;
};

struct FrameGenerationRecord {
  int frame_index = 0;
  std::string started_at_utc;
  std::string completed_at_utc;
  std::optional<uint64_t> engine_request_id;
  std::string request_prompt;
  std::string composed_prompt;
  std::vector<std::string> reference_digests;
  absl::Status status;
  std::optional<ImageGenerationResult> result;
  std::vector<RgbaImage> raw_outputs;
  std::vector<std::string> raw_output_digests;
};

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

absl::StatusOr<std::string> ReadBoundedText(const std::filesystem::path& path, size_t maximum_bytes,
                                            std::string_view context) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return absl::NotFoundError(absl::StrCat(context, " is not a regular file: ", path.string(),
                                            error ? absl::StrCat(": ", error.message()) : ""));
  }
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    return absl::NotFoundError(absl::StrCat("could not size ", context, ": ", error.message()));
  }
  if (size == 0 || size > maximum_bytes) {
    return absl::ResourceExhaustedError(
        absl::StrCat(context, " is empty or exceeds its ", maximum_bytes, "-byte limit"));
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open ", context, ": ", path.string()));
  }
  std::string text(static_cast<size_t>(size), '\0');
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(text.size())) {
    return absl::DataLossError(absl::StrCat("could not read complete ", context));
  }
  return text;
}

absl::StatusOr<nlohmann::json> ReadBoundedJson(const std::filesystem::path& path,
                                               size_t maximum_bytes, std::string_view context) {
  ASSIGN_OR_RETURN(const std::string text, ReadBoundedText(path, maximum_bytes, context));
  const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
  if (document.is_discarded()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is not valid JSON"));
  }
  return document;
}

absl::Status ValidateRelativePath(std::string_view value, std::string_view context) {
  const std::filesystem::path path(value);
  if (value.empty() || path.is_absolute() || path.has_root_path() || path == "." ||
      path.lexically_normal() != path) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be a normalized relative path"));
  }
  for (const std::filesystem::path& component : path) {
    if (component == "..") {
      return absl::InvalidArgumentError(absl::StrCat(context, " cannot escape its manifest"));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::filesystem::path> ResolveConfinedFile(const std::filesystem::path& root,
                                                          std::string_view relative,
                                                          std::string_view context) {
  RETURN_IF_ERROR(ValidateRelativePath(relative, context));
  std::error_code error;
  const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not resolve ", context, " root: ", error.message()));
  }
  const std::filesystem::path candidate =
      std::filesystem::weakly_canonical(canonical_root / std::filesystem::path(relative), error);
  if (error) {
    return absl::NotFoundError(absl::StrCat("could not resolve ", context, ": ", error.message()));
  }
  const std::filesystem::path confined = candidate.lexically_relative(canonical_root);
  if (confined.empty() || confined.is_absolute() || confined == "." || *confined.begin() == "..") {
    return absl::PermissionDeniedError(absl::StrCat(context, " escapes its manifest directory"));
  }
  if (!std::filesystem::is_regular_file(candidate, error) || error) {
    return absl::NotFoundError(
        absl::StrCat(context, " is not a regular file: ", candidate.string()));
  }
  return candidate;
}

absl::StatusOr<HeadlessImageReferenceSource> ParseReferenceSource(const nlohmann::json& source,
                                                                  ImageGenerationReferenceRole role,
                                                                  std::string_view context) {
  if (!source.is_object() || source.size() != 1 ||
      (!source.contains("path") && !source.contains("source_artwork_id"))) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " must contain exactly one of path or source_artwork_id"));
  }
  HeadlessImageReferenceSource result{.role = role};
  if (source.contains("path")) {
    ASSIGN_OR_RETURN(result.path, Required<std::string>(source, "path", context));
  } else {
    ASSIGN_OR_RETURN(result.source_artwork_id,
                     Required<std::string>(source, "source_artwork_id", context));
  }
  RETURN_IF_ERROR(ValidateHeadlessImageReferenceSource(result));
  return result;
}

absl::StatusOr<ImageTransparencyPreference> ParseTransparency(std::string_view value) {
  if (value == "no-preference") return ImageTransparencyPreference::kNoPreference;
  if (value == "prefer-transparent") return ImageTransparencyPreference::kPreferTransparent;
  return absl::InvalidArgumentError(
      "pose batch generation transparency must be no-preference or prefer-transparent");
}

std::string_view ResultProviderForAdapter(std::string_view provider) {
  if (provider == "codex") return "openai-codex";
  return provider;
}

absl::StatusOr<LockedExperiment> LoadLockedExperiment(const std::filesystem::path& path) {
  if (path.empty()) {
    return absl::InvalidArgumentError("pose batch manifest path must be non-empty");
  }
  std::error_code error;
  const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error);
  if (error) {
    return absl::NotFoundError(
        absl::StrCat("could not resolve pose batch manifest: ", error.message()));
  }
  ASSIGN_OR_RETURN(
      const nlohmann::json document,
      ReadBoundedJson(canonical_path, kMaximumExperimentManifestBytes, "pose batch manifest"));
  RETURN_IF_ERROR(RequireExactObject(
      document,
      {"schema_version", "experiment_id", "animation_run_manifest", "identity_source",
       "pose_sheet_source", "pose_sheet_layout", "generation"},
      "pose batch manifest"));
  ASSIGN_OR_RETURN(const int schema_version,
                   Required<int>(document, "schema_version", "pose batch manifest"));
  if (schema_version != 1) {
    return absl::FailedPreconditionError("unsupported pose batch manifest schema; expected 1");
  }

  LockedExperiment result{.manifest_path = canonical_path};
  ASSIGN_OR_RETURN(result.experiment_id,
                   Required<std::string>(document, "experiment_id", "pose batch manifest"));
  if (result.experiment_id.empty()) {
    return absl::InvalidArgumentError("pose batch experiment_id must be non-empty");
  }
  ASSIGN_OR_RETURN(
      const std::string run_manifest,
      Required<std::string>(document, "animation_run_manifest", "pose batch manifest"));
  ASSIGN_OR_RETURN(
      result.animation_run_manifest_path,
      ResolveConfinedFile(canonical_path.parent_path(), run_manifest, "animation_run_manifest"));
  ASSIGN_OR_RETURN(result.identity_source,
                   ParseReferenceSource(document.at("identity_source"),
                                        ImageGenerationReferenceRole::kSubjectIdentity,
                                        "pose batch identity_source"));
  ASSIGN_OR_RETURN(
      result.pose_sheet_source,
      ParseReferenceSource(document.at("pose_sheet_source"), ImageGenerationReferenceRole::kPose,
                           "pose batch pose_sheet_source"));
  const nlohmann::json& pose_layout = document.at("pose_sheet_layout");
  RETURN_IF_ERROR(RequireExactObject(
      pose_layout,
      {"grid_x", "grid_y", "cell_width", "cell_height", "column_gap", "row_gap", "columns", "rows"},
      "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.grid_x,
                   Required<int>(pose_layout, "grid_x", "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.grid_y,
                   Required<int>(pose_layout, "grid_y", "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.cell_width,
                   Required<int>(pose_layout, "cell_width", "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.cell_height,
                   Required<int>(pose_layout, "cell_height", "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.column_gap,
                   Required<int>(pose_layout, "column_gap", "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.row_gap,
                   Required<int>(pose_layout, "row_gap", "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.columns,
                   Required<int>(pose_layout, "columns", "pose batch pose_sheet_layout"));
  ASSIGN_OR_RETURN(result.pose_sheet_layout.rows,
                   Required<int>(pose_layout, "rows", "pose batch pose_sheet_layout"));
  if (result.pose_sheet_layout.grid_x < 0 || result.pose_sheet_layout.grid_y < 0 ||
      result.pose_sheet_layout.cell_width <= 0 || result.pose_sheet_layout.cell_height <= 0 ||
      result.pose_sheet_layout.column_gap < 0 || result.pose_sheet_layout.row_gap < 0 ||
      result.pose_sheet_layout.columns != kColumns || result.pose_sheet_layout.rows != kRows) {
    return absl::InvalidArgumentError(
        "pose_sheet_layout must describe exactly 6x2 positive in-order cells");
  }

  const nlohmann::json& generation = document.at("generation");
  RETURN_IF_ERROR(
      RequireExactObject(generation,
                         {"provider", "model", "instructions", "prompt", "negative_requirements",
                          "target_aspect", "transparency", "expected_output"},
                         "pose batch generation"));
  ASSIGN_OR_RETURN(result.provider,
                   Required<std::string>(generation, "provider", "pose batch generation"));
  ASSIGN_OR_RETURN(result.model,
                   Required<std::string>(generation, "model", "pose batch generation"));
  ASSIGN_OR_RETURN(result.instructions,
                   Required<std::string>(generation, "instructions", "pose batch generation"));
  ASSIGN_OR_RETURN(result.prompt,
                   Required<std::string>(generation, "prompt", "pose batch generation"));
  ASSIGN_OR_RETURN(
      result.negative_requirements,
      Required<std::string>(generation, "negative_requirements", "pose batch generation"));
  if (result.provider.empty() || result.model.empty() || result.instructions.empty() ||
      result.prompt.empty() || result.negative_requirements.empty()) {
    return absl::InvalidArgumentError(
        "pose batch provider, model, instructions, prompt, and negative_requirements must be "
        "non-empty");
  }
  if (result.provider != "fake" && result.provider != "openai" && result.provider != "codex") {
    return absl::InvalidArgumentError("pose batch provider must be fake, openai, or codex");
  }

  const nlohmann::json& aspect = generation.at("target_aspect");
  RETURN_IF_ERROR(
      RequireExactObject(aspect, {"width", "height"}, "pose batch generation target_aspect"));
  ASSIGN_OR_RETURN(result.target_aspect.width,
                   Required<int>(aspect, "width", "pose batch generation target_aspect"));
  ASSIGN_OR_RETURN(result.target_aspect.height,
                   Required<int>(aspect, "height", "pose batch generation target_aspect"));
  if (result.target_aspect.width <= 0 || result.target_aspect.height <= 0) {
    return absl::InvalidArgumentError("pose batch target_aspect dimensions must be positive");
  }
  ASSIGN_OR_RETURN(const std::string transparency,
                   Required<std::string>(generation, "transparency", "pose batch generation"));
  ASSIGN_OR_RETURN(result.transparency, ParseTransparency(transparency));

  const nlohmann::json& output = generation.at("expected_output");
  RETURN_IF_ERROR(
      RequireExactObject(output, {"width", "height"}, "pose batch generation expected_output"));
  ASSIGN_OR_RETURN(result.expected_output_width,
                   Required<int>(output, "width", "pose batch generation expected_output"));
  ASSIGN_OR_RETURN(result.expected_output_height,
                   Required<int>(output, "height", "pose batch generation expected_output"));
  if (result.expected_output_width <= 0 || result.expected_output_height <= 0) {
    return absl::InvalidArgumentError("pose batch expected output dimensions must be positive");
  }
  const int64_t output_pixels =
      static_cast<int64_t>(result.expected_output_width) * result.expected_output_height;
  if (output_pixels <= 0 || output_pixels > kMaximumBatchOutputPixels / kFrameCount) {
    return absl::ResourceExhaustedError(
        "pose batch expected outputs exceed the aggregate pixel limit");
  }
  const int aspect_divisor = std::gcd(result.target_aspect.width, result.target_aspect.height);
  const int output_divisor = std::gcd(result.expected_output_width, result.expected_output_height);
  if (result.target_aspect.width / aspect_divisor !=
          result.expected_output_width / output_divisor ||
      result.target_aspect.height / aspect_divisor !=
          result.expected_output_height / output_divisor) {
    return absl::InvalidArgumentError(
        "pose batch expected output ratio must match target_aspect exactly");
  }
  return result;
}

RgbaImage ExtractPose(const RgbaImage& sheet, const AnimationFeasibilitySheetLayout& layout,
                      int index) {
  const int column = index % layout.columns;
  const int row = index / layout.columns;
  const int left = layout.grid_x + column * (layout.cell_width + layout.column_gap);
  const int top = layout.grid_y + row * (layout.cell_height + layout.row_gap);
  RgbaImage result{
      .width = layout.cell_width,
      .height = layout.cell_height,
      .pixels =
          std::vector<uint8_t>(static_cast<size_t>(layout.cell_width) * layout.cell_height * 4),
  };
  for (int y = 0; y < layout.cell_height; ++y) {
    const size_t source_offset = (static_cast<size_t>(top + y) * sheet.width + left) * 4;
    const size_t destination_offset = static_cast<size_t>(y) * layout.cell_width * 4;
    std::copy_n(sheet.pixels.begin() + static_cast<ptrdiff_t>(source_offset),
                static_cast<size_t>(layout.cell_width) * 4,
                result.pixels.begin() + static_cast<ptrdiff_t>(destination_offset));
  }
  return result;
}

nlohmann::json SourceOriginJson(const HeadlessImageReferenceSource& source) {
  if (source.path.has_value()) return {{"kind", "path"}, {"path", *source.path}};
  return {{"kind", "source-artwork"}, {"source_artwork_id", *source.source_artwork_id}};
}

nlohmann::json AnimationContractJson(const AnimationArtworkRunManifest& config) {
  nlohmann::json timing = nlohmann::json::array();
  for (const int value : config.frames_per_cycle) timing.push_back(value);
  nlohmann::json planted = nlohmann::json::array();
  for (const bool value : config.planted_frames) planted.push_back(value);
  return {
      {"clip", AnimationFeasibilityClipId(config.clip)},
      {"sheet",
       {{"grid_x", config.sheet.grid_x},
        {"grid_y", config.sheet.grid_y},
        {"cell_width", config.sheet.cell_width},
        {"cell_height", config.sheet.cell_height},
        {"column_gap", config.sheet.column_gap},
        {"row_gap", config.sheet.row_gap},
        {"columns", config.sheet.columns},
        {"rows", config.sheet.rows}}},
      {"frames_per_cycle", std::move(timing)},
      {"planted_frames", std::move(planted)},
  };
}

absl::StatusOr<LockedInputs> ResolveLockedInputs(Api& api, const LockedExperiment& experiment) {
  ASSIGN_OR_RETURN(AnimationArtworkRunManifest animation,
                   LoadAnimationArtworkRunManifest(experiment.animation_run_manifest_path));
  if (animation.clip != AnimationFeasibilityClip::kLocomotionRight ||
      animation.sheet.columns != kColumns || animation.sheet.rows != kRows ||
      animation.sheet.grid_x != 0 || animation.sheet.grid_y != 0 ||
      animation.sheet.column_gap != 0 || animation.sheet.row_gap != 0 ||
      animation.sheet.cell_width != experiment.expected_output_width ||
      animation.sheet.cell_height != experiment.expected_output_height ||
      animation.frames_per_cycle.size() != kFrameCount ||
      animation.planted_frames.size() != kFrameCount) {
    return absl::InvalidArgumentError(
        "pose batch requires a 0-origin, gapless locomotion-right 6x2 run manifest whose cell "
        "geometry matches expected_output");
  }
  ASSIGN_OR_RETURN(ResolvedHeadlessImageReference identity,
                   ResolveHeadlessImageReference(api, experiment.identity_source,
                                                 experiment.manifest_path.parent_path(),
                                                 kMaximumReferencePixels));
  ASSIGN_OR_RETURN(ResolvedHeadlessImageReference pose_sheet,
                   ResolveHeadlessImageReference(api, experiment.pose_sheet_source,
                                                 experiment.manifest_path.parent_path(),
                                                 kMaximumReferencePixels));

  const AnimationFeasibilitySheetLayout& layout = experiment.pose_sheet_layout;
  const int64_t expected_width = static_cast<int64_t>(layout.grid_x) +
                                 static_cast<int64_t>(layout.columns) * layout.cell_width +
                                 static_cast<int64_t>(layout.columns - 1) * layout.column_gap;
  const int64_t expected_height = static_cast<int64_t>(layout.grid_y) +
                                  static_cast<int64_t>(layout.rows) * layout.cell_height +
                                  static_cast<int64_t>(layout.rows - 1) * layout.row_gap;
  if (expected_width > pose_sheet.image.width || expected_height > pose_sheet.image.height) {
    return absl::InvalidArgumentError(
        "pose_sheet_layout cells extend beyond the resolved pose-sheet image");
  }

  std::vector<RgbaImage> poses;
  std::vector<std::string> pose_digests;
  poses.reserve(kFrameCount);
  pose_digests.reserve(kFrameCount);
  nlohmann::json pose_cells = nlohmann::json::array();
  for (int index = 0; index < static_cast<int>(kFrameCount); ++index) {
    RgbaImage pose = ExtractPose(pose_sheet.image, layout, index);
    ASSIGN_OR_RETURN(std::string digest, RgbaImageDigest(pose));
    const int column = index % layout.columns;
    const int row = index / layout.columns;
    const int left = layout.grid_x + column * (layout.cell_width + layout.column_gap);
    const int top = layout.grid_y + row * (layout.cell_height + layout.row_gap);
    pose_cells.push_back({
        {"frame_index", index},
        {"crop", {{"x", left}, {"y", top}, {"width", pose.width}, {"height", pose.height}}},
        {"rgba_sha256", digest},
    });
    poses.push_back(std::move(pose));
    pose_digests.push_back(std::move(digest));
  }

  const std::string transparency =
      experiment.transparency == ImageTransparencyPreference::kPreferTransparent
          ? "prefer-transparent"
          : "no-preference";
  nlohmann::json request_shape{
      {"experiment_id", experiment.experiment_id},
      {"animation", AnimationContractJson(animation)},
      {"generation",
       {{"provider", experiment.provider},
        {"result_provider", ResultProviderForAdapter(experiment.provider)},
        {"model", experiment.model},
        {"instructions", experiment.instructions},
        {"prompt", experiment.prompt},
        {"negative_requirements", experiment.negative_requirements},
        {"requested_candidates", 1},
        {"target_aspect",
         {{"width", experiment.target_aspect.width}, {"height", experiment.target_aspect.height}}},
        {"transparency", transparency},
        {"expected_output",
         {{"width", experiment.expected_output_width},
          {"height", experiment.expected_output_height}}}}},
      {"identity",
       {{"role", "subject-identity"},
        {"origin", SourceOriginJson(identity.source)},
        {"width", identity.image.width},
        {"height", identity.image.height},
        {"rgba_sha256", identity.content_digest}}},
      {"pose_sheet",
       {{"role", "pose"},
        {"origin", SourceOriginJson(pose_sheet.source)},
        {"width", pose_sheet.image.width},
        {"height", pose_sheet.image.height},
        {"rgba_sha256", pose_sheet.content_digest},
        {"layout",
         {{"grid_x", layout.grid_x},
          {"grid_y", layout.grid_y},
          {"cell_width", layout.cell_width},
          {"cell_height", layout.cell_height},
          {"column_gap", layout.column_gap},
          {"row_gap", layout.row_gap},
          {"columns", layout.columns},
          {"rows", layout.rows}}},
        {"cells", std::move(pose_cells)}}},
  };
  return LockedInputs{
      .animation = std::move(animation),
      .identity = std::move(identity),
      .pose_sheet = std::move(pose_sheet),
      .poses = std::move(poses),
      .pose_digests = std::move(pose_digests),
      .request_shape = std::move(request_shape),
  };
}

std::string FramePrompt(std::string_view prompt, int frame_index) {
  return absl::StrCat(prompt, "\n\nAnimation frame ", frame_index,
                      " of 12. Render exactly one fresh right-facing character image for this "
                      "pose. Do not return a sheet, turnaround, wireframe, or edited reference "
                      "board.");
}

ImageGenerationSpec MakeSpec(const LockedExperiment& experiment, const LockedInputs& inputs,
                             int frame_index) {
  return {
      .prompt = FramePrompt(experiment.prompt, frame_index),
      .instructions = absl::StrCat(experiment.instructions, "\n\nNegative requirements:\n",
                                   experiment.negative_requirements),
      .negative_prompt = std::nullopt,
      .requested_candidates = 1,
      .target_aspect = experiment.target_aspect,
      .transparency = experiment.transparency,
      .references =
          {
              {.role = ImageGenerationReferenceRole::kSubjectIdentity,
               .image = inputs.identity.image},
              {.role = ImageGenerationReferenceRole::kPose,
               .image = inputs.poses[static_cast<size_t>(frame_index)]},
          },
  };
}

absl::Status PreflightService(ImageGenerationService& service, const LockedExperiment& experiment,
                              const LockedInputs& inputs) {
  const ImageGenerationCapabilities capabilities = service.engine().Capabilities();
  ImageGenerationSpec spec = MakeSpec(experiment, inputs, 0);
  RETURN_IF_ERROR(ValidateImageGenerationSpec(spec, capabilities));
  const int64_t reference_pixels =
      static_cast<int64_t>(inputs.identity.image.width) * inputs.identity.image.height +
      static_cast<int64_t>(inputs.poses.front().width) * inputs.poses.front().height;
  if (capabilities.maximum_reference_images < 2 ||
      capabilities.maximum_reference_pixels < reference_pixels) {
    return absl::FailedPreconditionError(
        "image provider cannot accept the locked identity-first, pose-second request shape");
  }
  return absl::OkStatus();
}

absl::Status AwaitFrame(ImageGenerationService& service, const LockedExperiment& experiment,
                        const LockedInputs& inputs, int frame_index,
                        FrameGenerationRecord& record) {
  record.frame_index = frame_index;
  record.started_at_utc = CurrentUtcTimestamp();
  record.request_prompt = FramePrompt(experiment.prompt, frame_index);
  record.reference_digests = {inputs.identity.content_digest,
                              inputs.pose_digests[static_cast<size_t>(frame_index)]};
  ImageGenerationSpec spec = MakeSpec(experiment, inputs, frame_index);
  record.composed_prompt = ComposeImageGenerationPrompt(spec);
  absl::StatusOr<uint64_t> submitted = service.engine().Submit(std::move(spec));
  if (!submitted.ok()) {
    record.completed_at_utc = CurrentUtcTimestamp();
    return submitted.status();
  }
  const uint64_t request_id = *submitted;
  record.engine_request_id = request_id;
  const absl::Time deadline = absl::Now() + kGenerationTimeout;
  absl::StatusOr<ImageGenerationResult> generated =
      absl::DeadlineExceededError("pose-conditioned image generation timed out");
  while (absl::Now() < deadline) {
    std::optional<GenerationEvent> event = service.engine().NextEvent(request_id);
    if (event.has_value()) {
      generated = std::move(event->result);
      break;
    }
    absl::SleepFor(absl::Milliseconds(2));
  }
  if (!generated.ok() && absl::IsDeadlineExceeded(generated.status())) {
    const absl::Status cancelled = service.engine().Cancel(request_id);
    if (!cancelled.ok()) {
      generated = absl::DeadlineExceededError(absl::StrCat(
          "pose-conditioned generation timed out and cancellation failed: ", cancelled.message()));
    }
  }
  record.completed_at_utc = CurrentUtcTimestamp();
  if (!generated.ok()) return generated.status();
  record.result = *std::move(generated);
  record.raw_outputs.reserve(record.result->candidates.size());
  record.raw_output_digests.reserve(record.result->candidates.size());
  for (ImageGenerationCandidate& candidate : record.result->candidates) {
    ASSIGN_OR_RETURN(std::string digest, RgbaImageDigest(candidate.image));
    record.raw_outputs.push_back(std::move(candidate.image));
    record.raw_output_digests.push_back(std::move(digest));
  }
  if (record.result->provider != ResultProviderForAdapter(experiment.provider) ||
      record.result->model != experiment.model) {
    return absl::DataLossError(
        "image generation result provider/model does not match the locked request shape");
  }
  if (record.result->submitted_prompt != record.request_prompt) {
    return absl::DataLossError(
        "image generation result submitted_prompt does not match the frame request");
  }
  if (record.result->candidates.size() != 1) {
    return absl::DataLossError("pose-conditioned request must return exactly one candidate");
  }
  if (record.raw_outputs.front().width != experiment.expected_output_width ||
      record.raw_outputs.front().height != experiment.expected_output_height) {
    return absl::DataLossError(
        "pose-conditioned output dimensions do not match the locked expected output");
  }
  return absl::OkStatus();
}

RgbaImage AssembleOutputs(const std::vector<FrameGenerationRecord>& records,
                          const LockedExperiment& experiment) {
  RgbaImage sheet{
      .width = kColumns * experiment.expected_output_width,
      .height = kRows * experiment.expected_output_height,
      .pixels =
          std::vector<uint8_t>(static_cast<size_t>(kColumns) * experiment.expected_output_width *
                               kRows * experiment.expected_output_height * 4),
  };
  for (size_t index = 0; index < records.size(); ++index) {
    const RgbaImage& frame = records[index].raw_outputs.front();
    const int left = static_cast<int>(index % kColumns) * frame.width;
    const int top = static_cast<int>(index / kColumns) * frame.height;
    for (int y = 0; y < frame.height; ++y) {
      const size_t source_offset = static_cast<size_t>(y) * frame.width * 4;
      const size_t destination_offset = (static_cast<size_t>(top + y) * sheet.width + left) * 4;
      std::copy_n(frame.pixels.begin() + static_cast<ptrdiff_t>(source_offset),
                  static_cast<size_t>(frame.width) * 4,
                  sheet.pixels.begin() + static_cast<ptrdiff_t>(destination_offset));
    }
  }
  return sheet;
}

absl::StatusOr<AnimationArtworkFeasibilityConfig> GeneratedSheetConfig(
    const LockedExperiment& experiment, const LockedInputs& inputs) {
  ASSIGN_OR_RETURN(AnimationArtworkFeasibilityConfig config,
                   MakeAnimationArtworkRunManifestConfig(inputs.animation,
                                                         AnimationArtworkFeasibilityPalette()));
  config.maximum_source_width = kColumns * experiment.expected_output_width;
  config.maximum_source_height = kRows * experiment.expected_output_height;
  const int64_t assembled_pixels =
      static_cast<int64_t>(config.maximum_source_width) * config.maximum_source_height;
  config.maximum_source_pixels = static_cast<size_t>(assembled_pixels);
  return config;
}

absl::StatusOr<PilotApproval> ValidatePilotApproval(const std::filesystem::path& approval_path,
                                                    const nlohmann::json& request_shape) {
  if (approval_path.empty()) {
    return absl::FailedPreconditionError("batch phase requires --pilot_approval");
  }
  std::error_code error;
  const std::filesystem::path canonical_approval =
      std::filesystem::weakly_canonical(approval_path, error);
  if (error) {
    return absl::NotFoundError(absl::StrCat("could not resolve pilot approval: ", error.message()));
  }
  ASSIGN_OR_RETURN(const nlohmann::json approval,
                   ReadBoundedJson(canonical_approval, kMaximumApprovalBytes, "pilot approval"));
  RETURN_IF_ERROR(RequireExactObject(approval,
                                     {"schema_version", "decision", "reviewer", "reviewed_at_utc",
                                      "pilot_manifest", "pilot_run_id", "checks"},
                                     "pilot approval"));
  ASSIGN_OR_RETURN(const int schema_version,
                   Required<int>(approval, "schema_version", "pilot approval"));
  ASSIGN_OR_RETURN(const std::string decision,
                   Required<std::string>(approval, "decision", "pilot approval"));
  ASSIGN_OR_RETURN(const std::string reviewer,
                   Required<std::string>(approval, "reviewer", "pilot approval"));
  ASSIGN_OR_RETURN(const std::string reviewed_at,
                   Required<std::string>(approval, "reviewed_at_utc", "pilot approval"));
  ASSIGN_OR_RETURN(const std::string pilot_run_id,
                   Required<std::string>(approval, "pilot_run_id", "pilot approval"));
  if (schema_version != 1 || decision != "approved" || reviewer.empty() || reviewed_at.empty() ||
      pilot_run_id.empty()) {
    return absl::FailedPreconditionError(
        "pilot approval must be schema 1, explicitly approved, reviewed, and identify its run");
  }
  const nlohmann::json& checks = approval.at("checks");
  RETURN_IF_ERROR(RequireExactObject(
      checks,
      {"frame_0_fresh_single_render", "frame_0_identity_preserved", "frame_0_pose_obeyed",
       "frame_6_fresh_single_render", "frame_6_identity_preserved", "frame_6_pose_obeyed"},
      "pilot approval checks"));
  for (const auto& [name, value] : checks.items()) {
    if (!value.is_boolean() || !value.get<bool>()) {
      return absl::FailedPreconditionError(
          absl::StrCat("pilot approval check must be true: ", name));
    }
  }

  ASSIGN_OR_RETURN(const std::string pilot_manifest_relative,
                   Required<std::string>(approval, "pilot_manifest", "pilot approval"));
  ASSIGN_OR_RETURN(const std::filesystem::path pilot_manifest_path,
                   ResolveConfinedFile(canonical_approval.parent_path(), pilot_manifest_relative,
                                       "pilot approval pilot_manifest"));
  ASSIGN_OR_RETURN(
      const nlohmann::json pilot,
      ReadBoundedJson(pilot_manifest_path, kMaximumPilotManifestBytes, "approved pilot manifest"));
  RETURN_IF_ERROR(RequireExactObject(
      pilot,
      {"schema_version", "bundle", "run_id", "phase", "status", "classification",
       "animation_candidate", "created_at_utc", "completed_at_utc", "locked_request", "requests",
       "artifacts", "processing", "pilot_approval", "failure"},
      "approved pilot manifest"));
  if (pilot.at("schema_version") != 1 ||
      pilot.at("bundle") != "pose-conditioned-animation-evidence" || pilot.at("phase") != "pilot" ||
      pilot.at("status") != "complete" || pilot.at("classification") != "non-candidate-evidence" ||
      pilot.at("animation_candidate") != false || pilot.at("run_id") != pilot_run_id ||
      pilot.at("locked_request") != request_shape) {
    return absl::FailedPreconditionError(
        "pilot approval does not identify a complete non-candidate pilot for this request shape");
  }
  if (!pilot.at("requests").is_array() || pilot.at("requests").size() != 2) {
    return absl::FailedPreconditionError(
        "approved pilot must contain fresh complete frame-0 and frame-6 requests");
  }
  const nlohmann::json& pilot_requests = pilot.at("requests");
  ASSIGN_OR_RETURN(const int first_index,
                   Required<int>(pilot_requests[0], "frame_index", "approved pilot request 0"));
  ASSIGN_OR_RETURN(const int second_index,
                   Required<int>(pilot_requests[1], "frame_index", "approved pilot request 1"));
  ASSIGN_OR_RETURN(const std::string first_status,
                   Required<std::string>(pilot_requests[0], "status", "approved pilot request 0"));
  ASSIGN_OR_RETURN(const std::string second_status,
                   Required<std::string>(pilot_requests[1], "status", "approved pilot request 1"));
  if (first_index != kPilotFirstFrame || second_index != kPilotSecondFrame ||
      first_status != "complete" || second_status != "complete") {
    return absl::FailedPreconditionError(
        "approved pilot must contain fresh complete frame-0 and frame-6 requests");
  }
  return PilotApproval{
      .approval_path = canonical_approval,
      .pilot_manifest_path = pilot_manifest_path,
      .pilot_run_id = pilot_run_id,
      .evidence = approval,
  };
}

absl::Status CopyFile(const std::filesystem::path& source,
                      const std::filesystem::path& destination) {
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not create evidence directory: ", error.message()));
  }
  if (!std::filesystem::copy_file(source, destination, error)) {
    return absl::InternalError(absl::StrCat("could not copy evidence file: ",
                                            error ? error.message() : "source is missing"));
  }
  return absl::OkStatus();
}

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& document) {
  std::ofstream stream(path);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not open JSON evidence: ", path.string()));
  }
  stream << document.dump(2) << '\n';
  if (!stream.good()) {
    return absl::InternalError(absl::StrCat("could not write JSON evidence: ", path.string()));
  }
  return absl::OkStatus();
}

nlohmann::json FailureJson(const absl::Status& status) {
  if (status.ok()) return nullptr;
  return {{"code", static_cast<int>(status.code())}, {"message", status.message()}};
}

nlohmann::json SpriteFrameJson(const SpriteFrame& frame) {
  return {
      {"index", frame.index},         {"texture_x", frame.texture_x},
      {"texture_y", frame.texture_y}, {"texture_w", frame.texture_w},
      {"texture_h", frame.texture_h}, {"render_w", frame.render_w},
      {"render_h", frame.render_h},   {"frames_per_cycle", frame.frames_per_cycle},
      {"offset_x", frame.offset_x},   {"offset_y", frame.offset_y},
  };
}

absl::Status PublishEvidence(const PoseConditionedAnimationRunRequest& request,
                             const LockedExperiment& experiment, const LockedInputs& inputs,
                             const std::optional<PilotApproval>& approval,
                             const std::string& run_id, const std::string& created_at_utc,
                             const std::string& completed_at_utc,
                             const std::vector<FrameGenerationRecord>& records,
                             const std::optional<RgbaImage>& assembled,
                             const std::optional<AnimationArtworkFeasibilityResult>& processing,
                             const std::optional<absl::Status>& processing_status,
                             const absl::Status& execution_status) {
  const bool is_batch = request.phase == PoseConditionedAnimationPhase::kBatch;
  const bool animation_candidate = is_batch && execution_status.ok() &&
                                   records.size() == kFrameCount && assembled.has_value() &&
                                   processing.has_value();
  return PublishNewDirectoryAtomically(
      request.output_path.string(), [&](const std::filesystem::path& staging) -> absl::Status {
        nlohmann::json artifacts = nlohmann::json::array();
        const auto write_image = [&](std::string_view id, const std::string& relative_path,
                                     const RgbaImage& image, const std::string& digest,
                                     nlohmann::json metadata =
                                         nlohmann::json::object()) -> absl::Status {
          RETURN_IF_ERROR(WritePng((staging / relative_path).string(), image.width, image.height,
                                   image.pixels));
          artifacts.push_back({
              {"id", id},
              {"path", relative_path},
              {"width", image.width},
              {"height", image.height},
              {"rgba_sha256", digest},
              {"metadata", std::move(metadata)},
          });
          return absl::OkStatus();
        };

        RETURN_IF_ERROR(
            CopyFile(experiment.manifest_path, staging / "inputs/locked-experiment-manifest.json"));
        RETURN_IF_ERROR(CopyFile(experiment.animation_run_manifest_path,
                                 staging / "inputs/animation-run-manifest.json"));
        RETURN_IF_ERROR(write_image(
            "subject-identity", "inputs/subject-identity.png", inputs.identity.image,
            inputs.identity.content_digest,
            {{"role", "subject-identity"}, {"origin", SourceOriginJson(inputs.identity.source)}}));
        RETURN_IF_ERROR(write_image(
            "pose-sheet", "inputs/pose-sheet.png", inputs.pose_sheet.image,
            inputs.pose_sheet.content_digest,
            {{"role", "pose"}, {"origin", SourceOriginJson(inputs.pose_sheet.source)}}));
        for (size_t index = 0; index < inputs.poses.size(); ++index) {
          const auto& cell = inputs.request_shape.at("pose_sheet").at("cells").at(index);
          RETURN_IF_ERROR(
              write_image(absl::StrCat("pose-", index),
                          absl::StrCat("inputs/poses/pose-", index < 10 ? "0" : "", index, ".png"),
                          inputs.poses[index], inputs.pose_digests[index],
                          {{"frame_index", index}, {"crop", cell.at("crop")}}));
        }
        if (approval.has_value()) {
          RETURN_IF_ERROR(
              CopyFile(approval->approval_path, staging / "inputs/pilot-approval.json"));
          RETURN_IF_ERROR(CopyFile(approval->pilot_manifest_path,
                                   staging / "inputs/approved-pilot-manifest.json"));
        }

        nlohmann::json request_records = nlohmann::json::array();
        for (const FrameGenerationRecord& record : records) {
          nlohmann::json raw_outputs = nlohmann::json::array();
          for (size_t candidate_index = 0; candidate_index < record.raw_outputs.size();
               ++candidate_index) {
            const std::string relative =
                absl::StrCat("raw-outputs/frame-", record.frame_index < 10 ? "0" : "",
                             record.frame_index, "-candidate-", candidate_index, ".png");
            RETURN_IF_ERROR(write_image(
                absl::StrCat("raw-output-", record.frame_index, "-candidate-", candidate_index),
                relative, record.raw_outputs[candidate_index],
                record.raw_output_digests[candidate_index],
                {{"frame_index", record.frame_index},
                 {"candidate_index", candidate_index},
                 {"unmodified_provider_pixels", true}}));
            nlohmann::json candidate_revised_prompt = nullptr;
            if (record.result.has_value() &&
                record.result->candidates[candidate_index].revised_prompt.has_value()) {
              candidate_revised_prompt = *record.result->candidates[candidate_index].revised_prompt;
            }
            raw_outputs.push_back({{"candidate_index", candidate_index},
                                   {"path", relative},
                                   {"width", record.raw_outputs[candidate_index].width},
                                   {"height", record.raw_outputs[candidate_index].height},
                                   {"rgba_sha256", record.raw_output_digests[candidate_index]},
                                   {"revised_prompt", std::move(candidate_revised_prompt)}});
          }
          nlohmann::json provider_request_id = nullptr;
          nlohmann::json provider = nullptr;
          nlohmann::json model = nullptr;
          nlohmann::json submitted_prompt = nullptr;
          nlohmann::json revised_prompt = nullptr;
          if (record.result.has_value()) {
            provider = record.result->provider;
            model = record.result->model;
            submitted_prompt = record.result->submitted_prompt;
            if (record.result->provider_request_id.has_value()) {
              provider_request_id = *record.result->provider_request_id;
            }
            if (!record.result->candidates.empty() &&
                record.result->candidates.front().revised_prompt.has_value()) {
              revised_prompt = *record.result->candidates.front().revised_prompt;
            }
          }
          request_records.push_back({
              {"frame_index", record.frame_index},
              {"status", record.status.ok() ? "complete" : "failed"},
              {"failure", FailureJson(record.status)},
              {"started_at_utc", record.started_at_utc},
              {"completed_at_utc", record.completed_at_utc},
              {"engine_request_id", record.engine_request_id.has_value()
                                        ? nlohmann::json(*record.engine_request_id)
                                        : nlohmann::json(nullptr)},
              {"provider_request_id", std::move(provider_request_id)},
              {"provider", std::move(provider)},
              {"model", std::move(model)},
              {"request_prompt", record.request_prompt},
              {"composed_prompt", record.composed_prompt},
              {"submitted_prompt", std::move(submitted_prompt)},
              {"revised_prompt", std::move(revised_prompt)},
              {"reference_order", {"subject-identity", "pose"}},
              {"reference_rgba_sha256", record.reference_digests},
              {"raw_outputs", std::move(raw_outputs)},
          });
        }

        nlohmann::json processing_json = nullptr;
        if (assembled.has_value()) {
          ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(*assembled));
          RETURN_IF_ERROR(write_image(
              "assembled-source-sheet", "assembled-source-sheet.png", *assembled, digest,
              {{"columns", kColumns}, {"rows", kRows}, {"order", "row-major"}}));
        }
        if (processing.has_value()) {
          RETURN_IF_ERROR(write_image("processed-packed-texture", "processed/packed-texture.png",
                                      processing->packed_texture, processing->packed_digest));
          nlohmann::json frames = nlohmann::json::array();
          for (size_t index = 0; index < processing->frames.size(); ++index) {
            ASSIGN_OR_RETURN(const std::string digest,
                             RgbaImageDigest(processing->frames[index].finished));
            RETURN_IF_ERROR(write_image(
                absl::StrCat("processed-frame-", index),
                absl::StrCat("processed/frames/frame-", index < 10 ? "0" : "", index, ".png"),
                processing->frames[index].finished, digest, {{"frame_index", index}}));
            frames.push_back({
                {"index", processing->frames[index].diagnostics.index},
                {"visible_pixels", processing->frames[index].diagnostics.visible_pixels},
                {"contact_line_hit", processing->frames[index].diagnostics.contact_line_hit},
            });
          }
          nlohmann::json sprite_frames = nlohmann::json::array();
          for (const SpriteFrame& frame : processing->sprite_frames) {
            sprite_frames.push_back(SpriteFrameJson(frame));
          }
          processing_json = {
              {"passed", true},
              {"packed_rgba_sha256", processing->packed_digest},
              {"frames", std::move(frames)},
              {"sprite_frames", std::move(sprite_frames)},
          };
        } else if (processing_status.has_value()) {
          processing_json = {{"passed", false}, {"failure", FailureJson(*processing_status)}};
        }

        const nlohmann::json manifest{
            {"schema_version", 1},
            {"bundle", "pose-conditioned-animation-evidence"},
            {"run_id", run_id},
            {"phase", is_batch ? "batch" : "pilot"},
            {"status", execution_status.ok() ? "complete" : "failed"},
            {"classification",
             animation_candidate ? "animation-candidate" : "non-candidate-evidence"},
            {"animation_candidate", animation_candidate},
            {"created_at_utc", created_at_utc},
            {"completed_at_utc", completed_at_utc},
            {"locked_request", inputs.request_shape},
            {"requests", std::move(request_records)},
            {"artifacts", std::move(artifacts)},
            {"processing", std::move(processing_json)},
            {"pilot_approval", approval.has_value() ? approval->evidence : nlohmann::json(nullptr)},
            {"failure", FailureJson(execution_status)},
        };
        return WriteJson(staging / "manifest.json", manifest);
      });
}

}  // namespace

absl::StatusOr<PoseConditionedAnimationPhase> ParsePoseConditionedAnimationPhase(
    std::string_view value) {
  if (value == "pilot") return PoseConditionedAnimationPhase::kPilot;
  if (value == "batch") return PoseConditionedAnimationPhase::kBatch;
  return absl::InvalidArgumentError("pose-conditioned animation phase must be pilot or batch");
}

absl::StatusOr<PoseConditionedAnimationProviderConfig> LoadPoseConditionedAnimationProviderConfig(
    const std::filesystem::path& manifest_path) {
  ASSIGN_OR_RETURN(const LockedExperiment experiment, LoadLockedExperiment(manifest_path));
  return PoseConditionedAnimationProviderConfig{
      .provider = experiment.provider,
      .model = experiment.model,
      .expected_output_width = experiment.expected_output_width,
      .expected_output_height = experiment.expected_output_height,
  };
}

absl::Status RunPoseConditionedAnimationBatch(Api& api, ImageGenerationService& service,
                                              const PoseConditionedAnimationRunRequest& request) {
  if (request.output_path.empty()) {
    return absl::InvalidArgumentError("pose batch output path must be non-empty");
  }
  if (request.phase == PoseConditionedAnimationPhase::kPilot &&
      !request.pilot_approval_path.empty()) {
    return absl::InvalidArgumentError("pilot phase cannot consume pilot approval evidence");
  }
  RETURN_IF_ERROR(ValidateNewDirectoryDestination(request.output_path.string()));
  ASSIGN_OR_RETURN(const LockedExperiment experiment, LoadLockedExperiment(request.manifest_path));
  ASSIGN_OR_RETURN(const LockedInputs inputs, ResolveLockedInputs(api, experiment));
  std::optional<PilotApproval> approval;
  if (request.phase == PoseConditionedAnimationPhase::kBatch) {
    ASSIGN_OR_RETURN(approval,
                     ValidatePilotApproval(request.pilot_approval_path, inputs.request_shape));
  }
  RETURN_IF_ERROR(PreflightService(service, experiment, inputs));

  const std::vector<int> requested_frames =
      request.phase == PoseConditionedAnimationPhase::kPilot
          ? std::vector<int>{kPilotFirstFrame, kPilotSecondFrame}
          : std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  const std::string run_id = GenerateGuid();
  const std::string created_at_utc = CurrentUtcTimestamp();
  std::vector<FrameGenerationRecord> records;
  records.reserve(requested_frames.size());
  absl::Status execution_status = absl::OkStatus();
  for (const int frame_index : requested_frames) {
    FrameGenerationRecord record;
    record.status = AwaitFrame(service, experiment, inputs, frame_index, record);
    execution_status = record.status;
    records.push_back(std::move(record));
    if (!execution_status.ok()) break;
  }

  std::optional<RgbaImage> assembled;
  std::optional<AnimationArtworkFeasibilityResult> processing;
  std::optional<absl::Status> processing_status;
  if (execution_status.ok() && request.phase == PoseConditionedAnimationPhase::kBatch) {
    if (records.size() != kFrameCount) {
      execution_status = absl::DataLossError("batch did not produce the complete 0..11 frame set");
    } else {
      assembled = AssembleOutputs(records, experiment);
      absl::StatusOr<AnimationArtworkFeasibilityConfig> generated_config =
          GeneratedSheetConfig(experiment, inputs);
      if (!generated_config.ok()) {
        processing_status = generated_config.status();
        execution_status = generated_config.status();
      } else {
        absl::StatusOr<AnimationArtworkFeasibilityResult> processed =
            RunAnimationArtworkFeasibility(*assembled, *generated_config);
        if (!processed.ok()) {
          processing_status = processed.status();
          execution_status = processed.status();
        } else {
          processing = *std::move(processed);
        }
      }
    }
  }
  const std::string completed_at_utc = CurrentUtcTimestamp();
  RETURN_IF_ERROR(PublishEvidence(request, experiment, inputs, approval, run_id, created_at_utc,
                                  completed_at_utc, records, assembled, processing,
                                  processing_status, execution_status));
  if (!execution_status.ok()) {
    return absl::Status(execution_status.code(),
                        absl::StrCat(execution_status.message(), "; non-candidate evidence: ",
                                     (request.output_path / "manifest.json").string()));
  }
  return absl::OkStatus();
}

}  // namespace zebes
