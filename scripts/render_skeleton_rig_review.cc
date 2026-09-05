#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "artwork/skeleton_rig_review.h"
#include "common/status_macros.h"

ABSL_FLAG(std::string, rig, "", "Rig Bench schema-version-2 JSON document.");
ABSL_FLAG(std::string, clip, "run", "Clip key to render.");
ABSL_FLAG(std::string, output, "", "Destination standalone HTML review page.");
ABSL_FLAG(int, canvas_width, 256, "Skeleton coordinate-space width.");
ABSL_FLAG(int, canvas_height, 256, "Skeleton coordinate-space height.");

namespace {

absl::Status WriteReview(const std::filesystem::path& output_path, std::string_view html) {
  const std::filesystem::path parent = output_path.parent_path();
  if (!parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) {
      return absl::InternalError("could not create skeleton review output directory");
    }
  }
  std::ofstream stream(output_path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return absl::InternalError("could not open skeleton review output");
  }
  stream.write(html.data(), static_cast<std::streamsize>(html.size()));
  if (!stream) {
    return absl::DataLossError("could not write complete skeleton review output");
  }
  return absl::OkStatus();
}

absl::Status Run() {
  const std::string rig_path = absl::GetFlag(FLAGS_rig);
  const std::string clip_id = absl::GetFlag(FLAGS_clip);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  if (rig_path.empty() || clip_id.empty() || output_path.empty()) {
    return absl::InvalidArgumentError("--rig, --clip, and --output are required");
  }

  ASSIGN_OR_RETURN(const zebes::SkeletonRig rig, zebes::LoadSkeletonRig(rig_path));
  ASSIGN_OR_RETURN(const zebes::SkeletonRigClip* clip, zebes::FindSkeletonRigClip(rig, clip_id));
  ASSIGN_OR_RETURN(const zebes::SkeletonRigClipMetrics metrics,
                   zebes::MeasureSkeletonRigClip(rig, *clip));
  ASSIGN_OR_RETURN(const std::string html,
                   zebes::RenderSkeletonRigReviewHtml(rig, *clip, absl::GetFlag(FLAGS_canvas_width),
                                                      absl::GetFlag(FLAGS_canvas_height)));
  RETURN_IF_ERROR(WriteReview(output_path, html));
  std::cout << "Wrote " << output_path << " with " << clip->frames.size() << " frames; hip "
            << "oscillation " << metrics.hip_oscillation << " px; maximum bone drift "
            << metrics.maximum_bone_length_drift << " px.\n";
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);
  if (positional.size() != 1) {
    std::cerr << "Unexpected positional arguments.\n";
    return 2;
  }
  const absl::Status status = Run();
  if (!status.ok()) {
    std::cerr << status << '\n';
    return 1;
  }
  return 0;
}
