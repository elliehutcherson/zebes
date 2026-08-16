// Milestone-0 visual feasibility experiment for deterministic prop artwork.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "artwork/cleanup_prop.h"
#include "artwork/compose_prop.h"
#include "artwork/edge_treatment.h"
#include "artwork/isolate_subject.h"
#include "artwork/quantize_prop.h"
#include "artwork/rasterize_prop.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"
#include "terrain/terrain_generator.h"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_recipe.h"

namespace {

using ::zebes::ApplyPropEdgeTreatment;
using ::zebes::BuildPropPalette;
using ::zebes::CleanupAndValidateProp;
using ::zebes::ComposeProp;
using ::zebes::IsolateSubject;
using ::zebes::PropArtwork;
using ::zebes::PropCleanupConfig;
using ::zebes::PropCompositionConfig;
using ::zebes::PropEdgeConfig;
using ::zebes::PropPalette;
using ::zebes::PropPalettePolicy;
using ::zebes::PropRasterConfig;
using ::zebes::QuantizeProp;
using ::zebes::RasterizeProp;
using ::zebes::ReadPng;
using ::zebes::RenderShapeScene;
using ::zebes::ResolveTerrainPalette;
using ::zebes::RgbaImage;
using ::zebes::ShapeScene;
using ::zebes::SubjectIsolationConfig;
using ::zebes::TerrainPixelProfile;
using ::zebes::TerrainRecipe;
using ::zebes::TerrainRecipeFromJson;
using ::zebes::TerrainRenderer;
using ::zebes::TileShape;

absl::Status WriteImage(const std::string& path, const RgbaImage& image) {
  return zebes::WritePng(path, image.width, image.height, image.pixels);
}

absl::StatusOr<TerrainRecipe> ReadRecipe(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError(absl::StrCat("could not open ", path));
  nlohmann::json document;
  try {
    stream >> document;
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not parse terrain recipe ", path, ": ", error.what()));
  }
  return TerrainRecipeFromJson(document);
}

int PixelBlockSize(const TerrainRecipe& recipe) {
  if (recipe.config.pixel_profile != TerrainPixelProfile::kChunky16) return 1;
  return recipe.config.tile_size % 16 == 0 ? recipe.config.tile_size / 16 : 1;
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

RgbaImage ContactSheet(const std::vector<RgbaImage>& scenes) {
  constexpr int kGutter = 8;
  const int width = scenes.empty() ? 1
                                   : static_cast<int>(scenes.size()) * scenes[0].width +
                                         static_cast<int>(scenes.size() - 1) * kGutter;
  const int height = scenes.empty() ? 1 : scenes[0].height;
  RgbaImage sheet = Checkerboard(width, height);
  int left = 0;
  for (const RgbaImage& scene : scenes) {
    Composite(scene, left, 0, sheet);
    left += scene.width + kGutter;
  }
  return sheet;
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

const char* PolicyName(PropPalettePolicy policy) {
  switch (policy) {
    case PropPalettePolicy::kFullTerrain:
      return "full";
    case PropPalettePolicy::kSemanticSubset:
      return "semantic";
    case PropPalettePolicy::kDerivedRamps:
      return "derived";
  }
  return "unknown";
}

absl::Status Run(const std::string& source_path, const std::string& recipe_path,
                 const std::string& output_directory) {
  std::error_code create_error;
  std::filesystem::create_directories(output_directory, create_error);
  if (create_error) {
    return absl::UnavailableError(absl::StrCat("could not create output directory ",
                                               output_directory, ": ", create_error.message()));
  }

  ASSIGN_OR_RETURN(const RgbaImage source, ReadPng(source_path));
  ASSIGN_OR_RETURN(const TerrainRecipe recipe, ReadRecipe(recipe_path));
  ASSIGN_OR_RETURN(const zebes::ResolvedTerrainPalette terrain_palette,
                   ResolveTerrainPalette(recipe.config));
  ASSIGN_OR_RETURN(const RgbaImage isolated, IsolateSubject(source, SubjectIsolationConfig{}));
  ASSIGN_OR_RETURN(const PropArtwork composed, ComposeProp(isolated, PropCompositionConfig{}));
  ASSIGN_OR_RETURN(const PropArtwork rasterized,
                   RasterizeProp(composed, PropRasterConfig{
                                               .tile_size = recipe.config.tile_size,
                                               .canvas_tiles_wide = 3,
                                               .canvas_tiles_high = 2,
                                               .pixel_block_size = PixelBlockSize(recipe),
                                           }));
  ASSIGN_OR_RETURN(const TerrainRenderer renderer, TerrainRenderer::Create(recipe.config));
  ASSIGN_OR_RETURN(const RgbaImage terrain_scene, RenderComparisonTerrain(renderer));

  RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/isolated.png"), isolated));
  RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/composed.png"), composed.image));
  RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/rasterized.png"), rasterized.image));

  constexpr std::array<PropPalettePolicy, 3> kPolicies = {
      PropPalettePolicy::kFullTerrain,
      PropPalettePolicy::kSemanticSubset,
      PropPalettePolicy::kDerivedRamps,
  };
  std::vector<RgbaImage> contexts;
  for (const PropPalettePolicy policy : kPolicies) {
    ASSIGN_OR_RETURN(const PropPalette prop_palette,
                     BuildPropPalette(terrain_palette, recipe.config.material, policy));
    ASSIGN_OR_RETURN(const PropArtwork quantized, QuantizeProp(rasterized, prop_palette));
    ASSIGN_OR_RETURN(const PropArtwork outlined,
                     ApplyPropEdgeTreatment(quantized, prop_palette.outline, PropEdgeConfig{}));
    ASSIGN_OR_RETURN(
        const PropArtwork finished,
        CleanupAndValidateProp(outlined, prop_palette.colors,
                               PropCleanupConfig{.tile_size = recipe.config.tile_size}));

    const std::string name = PolicyName(policy);
    RETURN_IF_ERROR(
        WriteImage(absl::StrCat(output_directory, "/", name, "-prop.png"), finished.image));
    RgbaImage context = InContext(finished, terrain_scene, recipe.config.tile_size);
    RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/", name, "-context.png"), context));
    contexts.push_back(std::move(context));
    LOG(INFO) << name << " palette: " << prop_palette.colors.size() << " colours";
  }
  const RgbaImage comparison = ContactSheet(contexts);
  RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/comparison.png"), comparison));
  RETURN_IF_ERROR(WriteImage(absl::StrCat(output_directory, "/comparison-4x.png"),
                             ScaleNearest(comparison, 4)));
  LOG(INFO) << "wrote prop artwork comparison to " << output_directory;
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();
  if (argc != 4) {
    LOG(ERROR) << "Usage: " << argv[0] << " <source.png> <terrain_recipe.json> <output_dir>";
    return 1;
  }
  const absl::Status status = Run(argv[1], argv[2], argv[3]);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
    return 1;
  }
  return 0;
}
