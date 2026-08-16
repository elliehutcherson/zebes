// Milestone-0 visual feasibility experiment for deterministic prop artwork.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "artwork/prop_artwork_pipeline.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"
#include "terrain/terrain_generator.h"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_recipe.h"

namespace {

using ::zebes::PropArtwork;
using ::zebes::PropArtworkPipelineConfig;
using ::zebes::PropArtworkPipelineResult;
using ::zebes::PropArtworkStyle;
using ::zebes::ReadPng;
using ::zebes::RenderShapeScene;
using ::zebes::ResolveTerrainPalette;
using ::zebes::RgbaImage;
using ::zebes::RunPropArtworkPipeline;
using ::zebes::ShapeScene;
using ::zebes::TerrainGenConfig;
using ::zebes::TerrainPixelProfile;
using ::zebes::TerrainRecipe;
using ::zebes::TerrainRecipeFromJson;
using ::zebes::TerrainRenderer;
using ::zebes::TileShape;

absl::Status WriteImage(const std::string& path, const RgbaImage& image) {
  return zebes::WritePng(path, image.width, image.height, image.pixels);
}

absl::StatusOr<TerrainGenConfig> ReadTerrainConfig(const std::string& source) {
  constexpr std::string_view kPresetPrefix = "preset:";
  if (absl::StartsWith(source, kPresetPrefix)) {
    const std::string name = source.substr(kPresetPrefix.size());
    for (const zebes::TerrainPreset& preset : zebes::BuiltInTerrainPresets()) {
      if (preset.name == name) return preset.config;
    }
    return absl::NotFoundError(absl::StrCat("unknown built-in terrain preset ", name));
  }

  const std::string& path = source;
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));
  nlohmann::json document;
  try {
    stream >> document;
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not parse terrain recipe ", path, ": ", error.what()));
  }
  ASSIGN_OR_RETURN(const TerrainRecipe recipe, TerrainRecipeFromJson(document));
  return recipe.config;
}

int PixelBlockSize(const TerrainGenConfig& config) {
  if (config.pixel_profile != TerrainPixelProfile::kChunky16) return 1;
  return config.tile_size % 16 == 0 ? config.tile_size / 16 : 1;
}

RgbaImage Checkerboard(int width, int height) {
  RgbaImage image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t value = ((x / 8 + y / 8) % 2 == 0) ? 104 : 120;
      const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
      image.pixels[pixel + 0] = value;
      image.pixels[pixel + 1] = value;
      image.pixels[pixel + 2] = value;
      image.pixels[pixel + 3] = 255;
    }
  }
  return image;
}

void Composite(const RgbaImage& source, int left, int top, RgbaImage& destination) {
  for (int source_y = 0; source_y < source.height; ++source_y) {
    const int destination_y = top + source_y;
    if (destination_y < 0 || destination_y >= destination.height) continue;
    for (int source_x = 0; source_x < source.width; ++source_x) {
      const int destination_x = left + source_x;
      if (destination_x < 0 || destination_x >= destination.width) continue;
      const size_t source_pixel = (static_cast<size_t>(source_y) * source.width + source_x) * 4;
      const size_t destination_pixel =
          (static_cast<size_t>(destination_y) * destination.width + destination_x) * 4;
      const int source_alpha = source.pixels[source_pixel + 3];
      if (source_alpha == 0) continue;
      const int inverse_alpha = 255 - source_alpha;
      for (int channel = 0; channel < 3; ++channel) {
        destination.pixels[destination_pixel + channel] = static_cast<uint8_t>(
            (source.pixels[source_pixel + channel] * source_alpha +
             destination.pixels[destination_pixel + channel] * inverse_alpha + 127) /
            255);
      }
      destination.pixels[destination_pixel + 3] = 255;
    }
  }
}

RgbaImage InContext(const PropArtwork& prop, const RgbaImage& terrain, int tile_size) {
  RgbaImage scene = Checkerboard(terrain.width, terrain.height);
  const int contact_x = 4 * tile_size;
  const int contact_y = 3 * tile_size;
  Composite(prop.image, contact_x - prop.anchor_x, contact_y - prop.anchor_y, scene);
  Composite(terrain, 0, 0, scene);
  return scene;
}

absl::StatusOr<RgbaImage> RenderComparisonTerrain(const TerrainRenderer& renderer) {
  ShapeScene scene;
  scene.width = 8;
  scene.height = 5;
  scene.cells.assign(static_cast<size_t>(scene.width) * scene.height, TileShape::kNone);
  for (int y = 3; y < scene.height; ++y) {
    for (int x = 0; x < scene.width; ++x) {
      scene.cells[static_cast<size_t>(y) * scene.width + x] = TileShape::kFullBlock;
    }
  }
  return RenderShapeScene(renderer, scene);
}

RgbaImage ScaleNearest(const RgbaImage& source, int scale) {
  RgbaImage output;
  output.width = source.width * scale;
  output.height = source.height * scale;
  output.pixels.resize(static_cast<size_t>(output.width) * output.height * 4);
  for (int y = 0; y < output.height; ++y) {
    for (int x = 0; x < output.width; ++x) {
      const size_t source_pixel = (static_cast<size_t>(y / scale) * source.width + x / scale) * 4;
      const size_t output_pixel = (static_cast<size_t>(y) * output.width + x) * 4;
      std::copy_n(source.pixels.begin() + static_cast<ptrdiff_t>(source_pixel), 4,
                  output.pixels.begin() + static_cast<ptrdiff_t>(output_pixel));
    }
  }
  return output;
}

absl::Status Run(const std::string& source_path, const std::string& terrain_source,
                 const std::string& output_directory, int canvas_tiles_wide,
                 int canvas_tiles_high) {
  std::error_code create_error;
  std::filesystem::create_directories(output_directory, create_error);
  if (create_error) {
    return absl::UnavailableError(absl::StrCat("could not create output directory ",
                                               output_directory, ": ", create_error.message()));
  }

  ASSIGN_OR_RETURN(const RgbaImage source, ReadPng(source_path));
  ASSIGN_OR_RETURN(const TerrainGenConfig terrain_config, ReadTerrainConfig(terrain_source));
  ASSIGN_OR_RETURN(const zebes::ResolvedTerrainPalette terrain_palette,
                   ResolveTerrainPalette(terrain_config));
  const PropArtworkStyle style{
      .tile_size = terrain_config.tile_size,
      .pixel_block_size = PixelBlockSize(terrain_config),
      .palette = terrain_palette,
  };
  PropArtworkPipelineConfig pipeline_config;
  pipeline_config.composition.canvas_tiles_wide = canvas_tiles_wide;
  pipeline_config.composition.canvas_tiles_high = canvas_tiles_high;
  ASSIGN_OR_RETURN(const PropArtworkPipelineResult result,
                   RunPropArtworkPipeline(source, style, pipeline_config));

  ASSIGN_OR_RETURN(const TerrainRenderer renderer, TerrainRenderer::Create(terrain_config));
  ASSIGN_OR_RETURN(const RgbaImage terrain_scene, RenderComparisonTerrain(renderer));

  RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/isolated.png"), result.isolated));
  RETURN_IF_ERROR(
      WriteImage(absl::StrCat(output_directory, "/composed.png"), result.composed.image));
  RETURN_IF_ERROR(
      WriteImage(absl::StrCat(output_directory, "/rasterized.png"), result.rasterized.image));
  RETURN_IF_ERROR(
      WriteImage(absl::StrCat(output_directory, "/quantized.png"), result.quantized.image));
  RETURN_IF_ERROR(
      WriteImage(absl::StrCat(output_directory, "/edge-treated.png"), result.edge_treated.image));
  RETURN_IF_ERROR(
      WriteImage(absl::StrCat(output_directory, "/finished-prop.png"), result.finished.image));

  const RgbaImage context = InContext(result.finished, terrain_scene, terrain_config.tile_size);
  RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/context.png"), context));
  RETURN_IF_ERROR(
      WriteImage(absl::StrCat(output_directory, "/context-4x.png"), ScaleNearest(context, 4)));
  LOG(INFO) << "source digest: " << result.source_digest;
  LOG(INFO) << "full terrain palette: " << result.palette.colors.size() << " colours";
  LOG(INFO) << "wrote prop artwork comparison to " << output_directory;
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();
  if (argc != 4 && argc != 6) {
    LOG(ERROR) << "Usage: " << argv[0]
               << " <source.png> <terrain_recipe.json|preset:NAME> <output_dir>"
                  " [canvas_tiles_wide canvas_tiles_high]";
    return 1;
  }
  int canvas_tiles_wide = 3;
  int canvas_tiles_high = 2;
  if (argc == 6 && (!absl::SimpleAtoi(argv[4], &canvas_tiles_wide) ||
                    !absl::SimpleAtoi(argv[5], &canvas_tiles_high) || canvas_tiles_wide <= 0 ||
                    canvas_tiles_high <= 0)) {
    LOG(ERROR) << "canvas tile dimensions must be positive integers";
    return 1;
  }
  const absl::Status status = Run(argv[1], argv[2], argv[3], canvas_tiles_wide, canvas_tiles_high);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return 1;
  }
  return 0;
}
