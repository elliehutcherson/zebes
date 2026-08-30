#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "api/api.h"
#include "generation/image_generation_service.h"

namespace zebes {

enum class PoseConditionedAnimationPhase {
  kPilot,
  kBatch,
};

struct PoseConditionedAnimationProviderConfig {
  std::string provider;
  std::string model;
  int expected_output_width = 0;
  int expected_output_height = 0;
};

absl::StatusOr<PoseConditionedAnimationPhase> ParsePoseConditionedAnimationPhase(
    std::string_view value);

absl::StatusOr<PoseConditionedAnimationProviderConfig> LoadPoseConditionedAnimationProviderConfig(
    const std::filesystem::path& manifest_path);

struct PoseConditionedAnimationRunRequest {
  std::filesystem::path manifest_path;
  std::filesystem::path output_path;
  PoseConditionedAnimationPhase phase = PoseConditionedAnimationPhase::kPilot;
  // Required for batch and forbidden for pilot.
  std::filesystem::path pilot_approval_path;
};

// Runs the disposable pose-conditioned experiment through the shared
// ImageGenerationService. Generation is sequential and never retries. Once a
// provider request starts, any failure publishes auditable non-candidate
// evidence before returning the failure.
absl::Status RunPoseConditionedAnimationBatch(Api& api, ImageGenerationService& service,
                                              const PoseConditionedAnimationRunRequest& request);

}  // namespace zebes
