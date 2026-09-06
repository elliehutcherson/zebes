#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "artwork/skeleton_rig_review.h"
#include "common/status_macros.h"

ABSL_FLAG(std::string, rig, "", "Rig Bench schema-version-2 JSON document.");
ABSL_FLAG(std::string, clip, "run", "Clip key to render.");
ABSL_FLAG(std::string, output, "", "Destination standalone HTML review page.");
ABSL_FLAG(std::string, png_directory, "", "Optional directory for one PNG per frame.");
ABSL_FLAG(std::string, sheet_output, "", "Optional destination for one row-major PNG sheet.");
ABSL_FLAG(int, canvas_width, 256, "Skeleton coordinate-space width.");
ABSL_FLAG(int, canvas_height, 256, "Skeleton coordinate-space height.");
ABSL_FLAG(int, render_scale, 2, "Integer PNG scale applied to rig coordinates.");
ABSL_FLAG(int, sheet_columns, 6, "PNG sheet column count.");

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

absl::Status WriteFramePngs(const zebes::SkeletonRig& rig, const zebes::SkeletonRigClip& clip,
                            const std::filesystem::path& directory, int canvas_width,
                            int canvas_height, int render_scale) {
  for (size_t index = 0; index < clip.frames.size(); ++index) {
    ASSIGN_OR_RETURN(const zebes::RgbaImage image,
                     zebes::RenderSkeletonRigFrameImage(rig, clip.frames[index], canvas_width,
                                                        canvas_height, render_scale));
    const std::filesystem::path path =
        directory / absl::StrFormat("frame-%02d.png", static_cast<int>(index + 1));
    RETURN_IF_ERROR(zebes::WritePng(path.string(), image.width, image.height, image.pixels));
  }
  return absl::OkStatus();
}

absl::Status Run() {
  const std::string rig_path = absl::GetFlag(FLAGS_rig);
  const std::string clip_id = absl::GetFlag(FLAGS_clip);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  const std::string png_directory = absl::GetFlag(FLAGS_png_directory);
  const std::string sheet_output = absl::GetFlag(FLAGS_sheet_output);
  if (rig_path.empty() || clip_id.empty()) {
    return absl::InvalidArgumentError("--rig and --clip are required");
  }
  if (output_path.empty() && png_directory.empty() && sheet_output.empty()) {
    return absl::InvalidArgumentError(
        "at least one of --output, --png_directory, or --sheet_output is required");
  }

  const int canvas_width = absl::GetFlag(FLAGS_canvas_width);
  const int canvas_height = absl::GetFlag(FLAGS_canvas_height);
  const int render_scale = absl::GetFlag(FLAGS_render_scale);
  ASSIGN_OR_RETURN(const zebes::SkeletonRig rig, zebes::LoadSkeletonRig(rig_path));
  ASSIGN_OR_RETURN(const zebes::SkeletonRigClip* clip, zebes::FindSkeletonRigClip(rig, clip_id));
  ASSIGN_OR_RETURN(const zebes::SkeletonRigClipMetrics metrics,
                   zebes::MeasureSkeletonRigClip(rig, *clip));
  if (!output_path.empty()) {
    ASSIGN_OR_RETURN(const std::string html,
                     zebes::RenderSkeletonRigReviewHtml(rig, *clip, canvas_width, canvas_height));
    RETURN_IF_ERROR(WriteReview(output_path, html));
  }
  if (!png_directory.empty()) {
    RETURN_IF_ERROR(
        WriteFramePngs(rig, *clip, png_directory, canvas_width, canvas_height, render_scale));
  }
  if (!sheet_output.empty()) {
    ASSIGN_OR_RETURN(
        const zebes::RgbaImage sheet,
        zebes::RenderSkeletonRigSheetImage(rig, *clip, canvas_width, canvas_height, render_scale,
                                           absl::GetFlag(FLAGS_sheet_columns)));
    RETURN_IF_ERROR(zebes::WritePng(sheet_output, sheet.width, sheet.height, sheet.pixels));
  }
  std::cout << "Rendered " << clip->frames.size() << " frames; hip oscillation "
            << metrics.hip_oscillation << " px; maximum bone drift "
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
