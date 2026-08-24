#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "artwork/generated_artwork_postprocessor.h"
#include "common/image_io.h"

ABSL_FLAG(std::string, input, "", "Generated source PNG to process.");
ABSL_FLAG(std::string, output, "", "Destination PNG path.");
ABSL_FLAG(std::string, palette_reference, "", "PNG whose opaque colors define the output palette.");
ABSL_FLAG(int, output_width, 960, "Final image width in pixels.");
ABSL_FLAG(int, output_height, 540, "Final image height in pixels.");
ABSL_FLAG(int, background_red, 255, "Solid generated background red channel.");
ABSL_FLAG(int, background_green, 0, "Solid generated background green channel.");
ABSL_FLAG(int, background_blue, 255, "Solid generated background blue channel.");
ABSL_FLAG(double, transparent_distance, 24.0,
          "RGB distance from the background that becomes transparent.");
ABSL_FLAG(double, opaque_distance, 190.0,
          "RGB distance from the background that is trusted as opaque.");
ABSL_FLAG(int, alpha_threshold, 128, "Final binary-alpha threshold.");
ABSL_FLAG(int, minimum_transparent_border, 8,
          "Required clear border around the final visible bounds.");

namespace {

int Run() {
  const std::string input = absl::GetFlag(FLAGS_input);
  const std::string output = absl::GetFlag(FLAGS_output);
  const std::string palette_reference = absl::GetFlag(FLAGS_palette_reference);
  if (input.empty() || output.empty() || palette_reference.empty()) {
    std::cerr << "--input, --output, and --palette_reference are required\n";
    return 2;
  }
  const int red = absl::GetFlag(FLAGS_background_red);
  const int green = absl::GetFlag(FLAGS_background_green);
  const int blue = absl::GetFlag(FLAGS_background_blue);
  if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) {
    std::cerr << "background channels must be within [0, 255]\n";
    return 2;
  }

  const absl::StatusOr<zebes::RgbaImage> source = zebes::ReadPng(input);
  if (!source.ok()) {
    std::cerr << "could not read input: " << source.status().message() << "\n";
    return 1;
  }
  const absl::StatusOr<zebes::RgbaImage> reference = zebes::ReadPng(palette_reference);
  if (!reference.ok()) {
    std::cerr << "could not read palette reference: " << reference.status().message() << "\n";
    return 1;
  }

  const zebes::GeneratedArtworkPostprocessConfig config{
      .output_width = absl::GetFlag(FLAGS_output_width),
      .output_height = absl::GetFlag(FLAGS_output_height),
      .background = {static_cast<uint8_t>(red), static_cast<uint8_t>(green),
                     static_cast<uint8_t>(blue), 255},
      .transparent_distance = static_cast<float>(absl::GetFlag(FLAGS_transparent_distance)),
      .opaque_distance = static_cast<float>(absl::GetFlag(FLAGS_opaque_distance)),
      .final_alpha_threshold = absl::GetFlag(FLAGS_alpha_threshold),
      .minimum_transparent_border = absl::GetFlag(FLAGS_minimum_transparent_border),
  };
  const absl::StatusOr<zebes::GeneratedArtworkPostprocessResult> result =
      zebes::PostprocessGeneratedArtwork(*source, *reference, config);
  if (!result.ok()) {
    std::cerr << "postprocessing failed: " << result.status().message() << "\n";
    return 1;
  }
  const absl::Status written = zebes::WritePng(output, result->finished.width,
                                               result->finished.height, result->finished.pixels);
  if (!written.ok()) {
    std::cerr << "could not write output: " << written.message() << "\n";
    return 1;
  }

  const zebes::GeneratedArtworkBounds& bounds = result->diagnostics.visible_bounds;
  std::cout << "wrote " << output << "\n"
            << "palette colors: " << result->diagnostics.palette_colors << "\n"
            << "visible pixels: " << result->diagnostics.visible_pixels << "\n"
            << "partially matted source pixels: " << result->diagnostics.partially_matted_pixels
            << "\n"
            << "visible bounds: " << bounds.left << "," << bounds.top << " to " << bounds.right
            << "," << bounds.bottom << "\n";
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
