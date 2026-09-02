#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/isolate_subject.h"
#include "artwork/profile_silhouette.h"
#include "common/image_io.h"

ABSL_FLAG(std::string, input, "", "Generated profile-reference PNG.");
ABSL_FLAG(std::string, output, "", "Destination medial-axis evidence PNG.");
ABSL_FLAG(std::string, control_output, "",
          "Optional destination for binary neutral Canny control PNG.");
ABSL_FLAG(std::string, isolated_output, "",
          "Optional destination for the production-isolated source PNG.");
ABSL_FLAG(int, working_size, 256, "Square silhouette analysis size.");
ABSL_FLAG(int, minimum_branch_length, 5,
          "Terminal medial-axis branches shorter than this are removed.");
ABSL_FLAG(double, background_distance, 48.0,
          "RGB distance from the border median removed as background.");
ABSL_FLAG(double, enclosed_background_distance, 8.0,
          "RGB distance that also removes enclosed backdrop pockets.");

namespace {

int Run() {
  const std::string input = absl::GetFlag(FLAGS_input);
  const std::string output = absl::GetFlag(FLAGS_output);
  if (input.empty() || output.empty()) {
    std::cerr << "--input and --output are required\n";
    return 2;
  }

  const absl::StatusOr<zebes::RgbaImage> source = zebes::ReadPng(input);
  if (!source.ok()) {
    std::cerr << "could not read input: " << source.status().message() << "\n";
    return 1;
  }
  const zebes::SubjectIsolationConfig isolation_config{
      .background_distance = static_cast<float>(absl::GetFlag(FLAGS_background_distance)),
      .enclosed_background_distance =
          static_cast<float>(absl::GetFlag(FLAGS_enclosed_background_distance)),
  };
  const absl::StatusOr<zebes::RgbaImage> isolated =
      zebes::IsolateSubject(*source, isolation_config);
  if (!isolated.ok()) {
    std::cerr << "could not isolate profile: " << isolated.status().message() << "\n";
    return 1;
  }
  const zebes::ProfileSilhouetteConfig config{
      .working_size = absl::GetFlag(FLAGS_working_size),
      .minimum_branch_length = absl::GetFlag(FLAGS_minimum_branch_length),
  };
  const absl::StatusOr<zebes::ProfileSilhouette> profile =
      zebes::ExtractProfileSilhouette(*isolated, config);
  if (!profile.ok()) {
    std::cerr << "could not extract profile silhouette: " << profile.status().message() << "\n";
    return 1;
  }
  const absl::StatusOr<zebes::RgbaImage> evidence =
      zebes::RenderProfileSilhouetteEvidence(*profile);
  if (!evidence.ok()) {
    std::cerr << "could not render profile evidence: " << evidence.status().message() << "\n";
    return 1;
  }
  const absl::Status written =
      zebes::WritePng(output, evidence->width, evidence->height, evidence->pixels);
  if (!written.ok()) {
    std::cerr << "could not write profile evidence: " << written.message() << "\n";
    return 1;
  }

  const std::string control_output = absl::GetFlag(FLAGS_control_output);
  if (!control_output.empty()) {
    const absl::StatusOr<zebes::RgbaImage> control =
        zebes::RenderProfileSilhouetteControl(*profile);
    if (!control.ok()) {
      std::cerr << "could not render profile control: " << control.status().message() << "\n";
      return 1;
    }
    const absl::Status control_written =
        zebes::WritePng(control_output, control->width, control->height, control->pixels);
    if (!control_written.ok()) {
      std::cerr << "could not write profile control: " << control_written.message() << "\n";
      return 1;
    }
  }

  const std::string isolated_output = absl::GetFlag(FLAGS_isolated_output);
  if (!isolated_output.empty()) {
    const absl::Status isolated_written =
        zebes::WritePng(isolated_output, isolated->width, isolated->height, isolated->pixels);
    if (!isolated_written.ok()) {
      std::cerr << "could not write isolated profile: " << isolated_written.message() << "\n";
      return 1;
    }
  }

  std::cout << "wrote " << output << "\n"
            << "working size: " << profile->width << "x" << profile->height << "\n"
            << "source scale: 1:" << profile->source_scale << "\n"
            << "silhouette pixels: " << profile->silhouette_pixels << "\n"
            << "medial-axis pixels: " << profile->medial_axis_pixels << "\n"
            << "components: " << profile->component_count << "\n"
            << "endpoints: " << profile->endpoint_count << "\n"
            << "branch pixels: " << profile->branch_pixel_count << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);
  if (positional.size() != 1) {
    std::cerr << "unexpected positional arguments\n";
    return 2;
  }
  return Run();
}
