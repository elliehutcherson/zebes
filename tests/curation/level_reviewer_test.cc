#include "curation/level_reviewer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api_mock.h"
#include "artwork/prepare_prop_asset.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "curation/raster_canvas.h"
#include "generation/generated_asset_candidate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"
#include "platform/headless/headless_texture_store.h"
#include "terrain/terrain_palette.h"
#include "terrain/terrain_style.h"

namespace zebes {
namespace {
using ::testing::Return;

class TemporaryReviewDirectory {
 public:
  TemporaryReviewDirectory()
      : path_(std::filesystem::temp_directory_path() /
              absl::StrCat("zebes-level-reviewer-test-", GenerateGuid())) {}
  ~TemporaryReviewDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

RgbaImage SolidImage(int width, int height, RgbaColor8 color) {
  RgbaImage image{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4),
  };
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    image.pixels[offset] = color.red;
    image.pixels[offset + 1] = color.green;
    image.pixels[offset + 2] = color.blue;
    image.pixels[offset + 3] = color.alpha;
  }
  return image;
}

const CurationArtifact* FindCompleteFrame(const CurationReview& review, double zoom) {
  for (const CurationArtifact& artifact : review.artifacts) {
    if (artifact.metadata.value("view", "") == "complete" &&
        artifact.metadata.at("camera").at("zoom") == zoom) {
      return &artifact;
    }
  }
  return nullptr;
}

const nlohmann::json* FindPublishedCompleteFrame(const nlohmann::json& manifest, double zoom) {
  for (const nlohmann::json& artifact : manifest.at("artifacts")) {
    if (artifact.at("metadata").value("view", "") == "complete" &&
        artifact.at("metadata").at("camera").at("zoom") == zoom) {
      return &artifact;
    }
  }
  return nullptr;
}

RgbaColor8 Pixel(const RgbaImage& image, int x, int y) {
  const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
  return {.red = image.pixels[offset],
          .green = image.pixels[offset + 1],
          .blue = image.pixels[offset + 2],
          .alpha = image.pixels[offset + 3]};
}

struct GeneratedPropFixture {
  nlohmann::json candidate;
  PreparedPropAsset prepared;
};

RgbaImage PropSourcePixels() {
  RgbaImage image = SolidImage(32, 24, {.red = 236, .green = 232, .blue = 228, .alpha = 255});
  for (int y = 6; y < 19; ++y) {
    for (int x = 7; x < 25; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      image.pixels[offset] = 40;
      image.pixels[offset + 1] = 210;
      image.pixels[offset + 2] = 240;
    }
  }
  return image;
}

absl::StatusOr<GeneratedPropFixture> MakeGeneratedPropFixture(
    const std::filesystem::path& candidate_root) {
  const RgbaImage source_pixels = PropSourcePixels();
  ASSIGN_OR_RETURN(const std::string source_digest, RgbaImageDigest(source_pixels));
  const SourceArtwork source{
      .id = "template-source-id",
      .name = "Preview prop source",
      .source_path = "source_art/template-source-id.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "preview-prop.png",
              .imported_at_utc = "2026-08-28T12:00:00Z",
          },
      .width = source_pixels.width,
      .height = source_pixels.height,
      .content_digest = source_digest,
  };
  const TerrainGenConfig terrain;
  ASSIGN_OR_RETURN(const ResolvedTerrainPalette palette, ResolveTerrainPalette(terrain));
  PreparePropAssetRequest template_request{
      .name = "Preview prop template",
      .style = {.tile_size = 8, .pixel_block_size = 1, .palette = palette},
      .ids = {.texture_id = "template-texture-id",
              .sprite_id = "template-sprite-id",
              .blueprint_id = "template-blueprint-id",
              .recipe_id = "template-recipe-id"},
  };
  template_request.pipeline.isolation.minimum_subject_area = 16;
  template_request.pipeline.composition = {
      .canvas_tiles_wide = 2,
      .canvas_tiles_high = 1,
      .padding_fraction = 0.05F,
  };
  template_request.pipeline.cleanup.contact_tolerance = 2;
  ASSIGN_OR_RETURN(PreparedPropAsset prepared,
                   PreparePropAsset(source, source_pixels, template_request));
  RETURN_IF_ERROR(WritePng((candidate_root / "processed-source.png").string(), source_pixels.width,
                           source_pixels.height, source_pixels.pixels));
  const GeneratedPropCreationCandidate candidate{
      .asset_id = "preview-prop-recipe-id",
      .name = "Transient preview prop",
      .source = {.relative_path = "processed-source.png",
                 .width = source_pixels.width,
                 .height = source_pixels.height,
                 .content_digest = source_digest,
                 .provenance = {.provider = "fake",
                                .model = "zebes-fake-v1",
                                .submitted_prompt = "a cyan preview prop",
                                .generated_at_utc = "2026-08-28T12:00:00Z"}},
      .template_recipe = prepared.recipe,
      .ids = {.texture_id = "preview-prop-texture-id",
              .sprite_id = "preview-prop-sprite-id",
              .blueprint_id = "preview-prop-blueprint-id",
              .recipe_id = "preview-prop-recipe-id"},
  };
  return GeneratedPropFixture{
      .candidate = GeneratedPropCreationCandidateToJson(candidate),
      .prepared = std::move(prepared),
  };
}

TEST(LevelReviewerTest, RendersParallaxTilesAndEntitiesInProductionOrder) {
  Level level{
      .id = "level-id",
      .name = "Integrated Level",
      .tileset_id = "tileset-id",
      .tile_render_width = 16,
      .tile_render_height = 16,
      .width = 1280,
      .height = 720,
      .spawn_point = {320, 360},
      .layers = {WorldLayer{.id = 0, .name = "Gameplay"}},
      .zones = {{.id = 0,
                 .name = "Room",
                 .theme_id = "theme-id",
                 .min_point = {0, 0},
                 .max_point = {1280, 720}}},
  };
  level.layers.front().tile_chunks[ChunkKey(1, 0)].tiles[22 * TileChunk::kSize + 8] = 1;
  level.layers.front().entities.emplace(
      1, Entity{.id = 1, .transform = {.position = {640, 360}}, .sprite_id = "sprite-id"});
  level.layers.front().entities.emplace(
      2, Entity{.id = 2, .transform = {.position = {640, 100}}, .sprite_id = "sprite-id"});

  ParallaxTheme theme{
      .id = "theme-id",
      .name = "Room Theme",
      .layers = {{.name = "Fill",
                  .scroll_factor = {0, 0},
                  .repeat_period = {64, 64},
                  .elements = {{.id = 0, .name = "Fill", .texture_id = "parallax-texture-id"}}}},
  };
  Tileset tileset{
      .id = "tileset-id",
      .name = "Tiles",
      .texture_id = "atlas-texture-id",
      .tile_width = 1,
      .tile_height = 1,
      .tiles = {{.id = 1,
                 .name = "Block",
                 .source_x = 0,
                 .source_y = 0,
                 .shape = TileShape::kFullBlock}},
  };
  Sprite sprite{
      .id = "sprite-id",
      .name = "Marker",
      .texture_id = "sprite-texture-id",
      .frames = {{.index = 0,
                  .texture_x = 0,
                  .texture_y = 0,
                  .texture_w = 1,
                  .texture_h = 1,
                  .render_w = 16,
                  .render_h = 16}},
  };
  Texture parallax_texture{.id = "parallax-texture-id", .name = "Parallax", .path = "parallax.png"};
  Texture atlas_texture{.id = "atlas-texture-id", .name = "Atlas", .path = "atlas.png"};
  Texture sprite_texture{.id = "sprite-texture-id", .name = "Sprite", .path = "sprite.png"};
  const RgbaImage parallax_pixels =
      SolidImage(64, 64, {.red = 20, .green = 40, .blue = 200, .alpha = 255});
  const RgbaImage atlas_pixels =
      SolidImage(1, 1, {.red = 20, .green = 200, .blue = 40, .alpha = 255});
  const RgbaImage sprite_pixels =
      SolidImage(1, 1, {.red = 220, .green = 30, .blue = 40, .alpha = 255});

  HeadlessTextureStore texture_store;
  ASSERT_OK_AND_ASSIGN(const TextureHandle parallax_handle,
                       texture_store.LoadFromPixels(parallax_pixels.width, parallax_pixels.height,
                                                    parallax_pixels.pixels));
  ASSERT_OK_AND_ASSIGN(
      const TextureHandle atlas_handle,
      texture_store.LoadFromPixels(atlas_pixels.width, atlas_pixels.height, atlas_pixels.pixels));
  ASSERT_OK_AND_ASSIGN(const TextureHandle sprite_handle,
                       texture_store.LoadFromPixels(sprite_pixels.width, sprite_pixels.height,
                                                    sprite_pixels.pixels));

  MockApi api;
  EXPECT_CALL(api, GetLevel(level.id)).Times(5).WillRepeatedly(Return(&level));
  EXPECT_CALL(api, GetTileset(tileset.id)).Times(5).WillRepeatedly(Return(&tileset));
  EXPECT_CALL(api, GetParallaxTheme(theme.id)).Times(5).WillRepeatedly(Return(&theme));
  EXPECT_CALL(api, GetSprite(sprite.id)).Times(5).WillRepeatedly(Return(&sprite));
  EXPECT_CALL(api, GetTexture(parallax_texture.id))
      .Times(5)
      .WillRepeatedly(Return(&parallax_texture));
  EXPECT_CALL(api, GetTexture(atlas_texture.id)).Times(5).WillRepeatedly(Return(&atlas_texture));
  EXPECT_CALL(api, GetTexture(sprite_texture.id)).Times(5).WillRepeatedly(Return(&sprite_texture));
  EXPECT_CALL(api, GetTextureHandle(parallax_texture.id))
      .Times(5)
      .WillRepeatedly(Return(parallax_handle));
  EXPECT_CALL(api, GetTextureHandle(atlas_texture.id))
      .Times(5)
      .WillRepeatedly(Return(atlas_handle));
  EXPECT_CALL(api, GetTextureHandle(sprite_texture.id))
      .Times(5)
      .WillRepeatedly(Return(sprite_handle));
  EXPECT_CALL(api, ReadTexturePixels(parallax_texture.id))
      .Times(5)
      .WillRepeatedly(Return(parallax_pixels));
  EXPECT_CALL(api, ReadTexturePixels(atlas_texture.id))
      .Times(5)
      .WillRepeatedly(Return(atlas_pixels));
  EXPECT_CALL(api, ReadTexturePixels(sprite_texture.id))
      .Times(5)
      .WillRepeatedly(Return(sprite_pixels));
  EXPECT_CALL(api, UpdateLevel).Times(0);
  EXPECT_CALL(api, CreateGeneratedProp).Times(0);
  EXPECT_CALL(api, CreateTextureFromPixels).Times(0);

  LevelReviewer reviewer;
  ASSERT_OK_AND_ASSIGN(CurationReview review, reviewer.Review(api, {.asset_id = level.id}));
  ASSERT_OK_AND_ASSIGN(CurationReview repeated_review,
                       reviewer.Review(api, {.asset_id = level.id}));
  ASSERT_OK_AND_ASSIGN(
      CurationReview focused_review,
      reviewer.Review(api, {.asset_id = level.id, .focus_entity_id = uint64_t{1}}));
  TemporaryReviewDirectory candidate_directory;
  ASSERT_TRUE(std::filesystem::create_directories(candidate_directory.path()));
  ASSERT_OK_AND_ASSIGN(const GeneratedPropFixture candidate,
                       MakeGeneratedPropFixture(candidate_directory.path()));
  const std::filesystem::path candidate_output = candidate_directory.path() / "published";
  ASSERT_OK_AND_ASSIGN(
      const size_t candidate_artifact_count,
      reviewer.PublishCandidateReview(api,
                                      {.asset_id = level.id,
                                       .candidate_root = candidate_directory.path().string(),
                                       .focus_entity_id = uint64_t{1}},
                                      candidate.candidate, candidate_output.string()));
  std::ifstream candidate_manifest_stream(candidate_output / "manifest.json");
  ASSERT_TRUE(candidate_manifest_stream.is_open());
  const nlohmann::json candidate_manifest = nlohmann::json::parse(candidate_manifest_stream);
  const nlohmann::json& candidate_review_metadata = candidate_manifest.at("metadata");

  EXPECT_EQ(review.kind, "level");
  EXPECT_EQ(review.metadata.at("route_count"), 4);
  EXPECT_EQ(review.metadata.at("sample_count"), 16);
  ASSERT_EQ(review.metadata.at("world_layers").size(), 1);
  EXPECT_EQ(review.metadata.at("world_layers").front().at("active_entity_count"), 2);
  EXPECT_EQ(review.metadata.at("world_layers").front().at("painted_tile_count"), 1);
  const CurationArtifact* frame = FindCompleteFrame(review, 0.5);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(Pixel(frame->image, 322, 181).red, 220);
  EXPECT_EQ(Pixel(frame->image, 322, 178).green, 200);
  EXPECT_EQ(Pixel(frame->image, 100, 100).blue, 200);
  EXPECT_NE(std::find_if(review.artifacts.begin(), review.artifacts.end(),
                         [](const CurationArtifact& artifact) {
                           return artifact.id == "contact-sheet-z050-zone-000-track-00";
                         }),
            review.artifacts.end());
  const auto layout =
      std::find_if(review.artifacts.begin(), review.artifacts.end(),
                   [](const CurationArtifact& artifact) { return artifact.id == "layout-map"; });
  ASSERT_NE(layout, review.artifacts.end());
  const RgbaColor8 one_x_route = Pixel(layout->image, 900, 327);
  EXPECT_EQ(one_x_route.red, 210);
  EXPECT_EQ(one_x_route.green, 120);
  EXPECT_EQ(one_x_route.blue, 255);
  const RgbaColor8 two_x_route = Pixel(layout->image, 900, 142);
  EXPECT_EQ(two_x_route.red, 255);
  EXPECT_EQ(two_x_route.green, 160);
  EXPECT_EQ(two_x_route.blue, 80);
  EXPECT_EQ(std::count_if(review.artifacts.begin(), review.artifacts.end(),
                          [](const CurationArtifact& artifact) {
                            return artifact.metadata.value("view", "") == "contact-sheet";
                          }),
            4);

  EXPECT_EQ(repeated_review.metadata, review.metadata);
  ASSERT_EQ(repeated_review.findings.size(), review.findings.size());
  for (size_t index = 0; index < review.findings.size(); ++index) {
    EXPECT_EQ(repeated_review.findings[index].severity, review.findings[index].severity);
    EXPECT_EQ(repeated_review.findings[index].code, review.findings[index].code);
    EXPECT_EQ(repeated_review.findings[index].subject, review.findings[index].subject);
    EXPECT_EQ(repeated_review.findings[index].message, review.findings[index].message);
  }
  ASSERT_EQ(repeated_review.artifacts.size(), review.artifacts.size());
  for (size_t index = 0; index < review.artifacts.size(); ++index) {
    const CurationArtifact& repeated = repeated_review.artifacts[index];
    const CurationArtifact& original = review.artifacts[index];
    EXPECT_EQ(repeated.id, original.id);
    EXPECT_EQ(repeated.relative_path, original.relative_path);
    EXPECT_EQ(repeated.description, original.description);
    EXPECT_EQ(repeated.image.width, original.image.width);
    EXPECT_EQ(repeated.image.height, original.image.height);
    EXPECT_EQ(repeated.image.pixels, original.image.pixels);
    EXPECT_EQ(repeated.metadata, original.metadata);
  }

  EXPECT_EQ(focused_review.metadata.at("review_mode"), "focused-entity");
  EXPECT_EQ(focused_review.metadata.at("focus").at("entity_id"), 1);
  EXPECT_EQ(focused_review.metadata.at("focus").at("world_layer_name"), "Gameplay");
  EXPECT_EQ(focused_review.metadata.at("route_count"), 3);
  EXPECT_EQ(focused_review.metadata.at("sample_count"), 3);
  EXPECT_LT(focused_review.artifacts.size(), review.artifacts.size());
  EXPECT_EQ(std::count_if(focused_review.artifacts.begin(), focused_review.artifacts.end(),
                          [](const CurationArtifact& artifact) {
                            return artifact.metadata.value("view", "") == "focused-entity";
                          }),
            3);
  const auto focused_frame =
      std::find_if(focused_review.artifacts.begin(), focused_review.artifacts.end(),
                   [](const CurationArtifact& artifact) {
                     return artifact.metadata.value("view", "") == "focused-entity" &&
                            artifact.metadata.at("camera").at("zoom") == 1.0;
                   });
  ASSERT_NE(focused_frame, focused_review.artifacts.end());
  EXPECT_EQ(focused_frame->metadata.at("focus").at("screen").at("color"), "#ffdc28");
  EXPECT_TRUE(
      focused_frame->metadata.at("focus").at("screen").at("intersects_viewport").get<bool>());
  const nlohmann::json& focused_screen = focused_frame->metadata.at("focus").at("screen");
  const int focused_x =
      static_cast<int>(std::lround(focused_screen.at("bounds").at("min").at("x").get<double>()));
  const int focused_y =
      static_cast<int>(std::lround(focused_screen.at("bounds").at("min").at("y").get<double>()));
  EXPECT_EQ(Pixel(focused_frame->image, focused_x, focused_y).red, 255);
  EXPECT_EQ(Pixel(focused_frame->image, focused_x, focused_y).green, 220);

  EXPECT_EQ(candidate_review_metadata.at("definition"), LevelToJson(level));
  const nlohmann::json& candidate_metadata =
      candidate_review_metadata.at("transient_prop_candidate");
  EXPECT_EQ(candidate_metadata.at("operation"), "create");
  EXPECT_EQ(candidate_metadata.at("asset_id"), "preview-prop-recipe-id");
  EXPECT_EQ(candidate_metadata.at("candidate_sprite_id"), "preview-prop-sprite-id");
  EXPECT_EQ(candidate_metadata.at("final_rgba_sha256"),
            candidate.prepared.recipe.final_pixel_digest);
  EXPECT_EQ(candidate_metadata.at("target").at("entity_id"), 1);
  EXPECT_EQ(candidate_metadata.at("target").at("persisted_sprite_id"), "sprite-id");
  EXPECT_FALSE(candidate_metadata.at("workspace_mutated").get<bool>());
  EXPECT_EQ(level.layers.front().entities.at(1).sprite_id, "sprite-id");
  EXPECT_EQ(candidate_artifact_count, focused_review.artifacts.size());
  EXPECT_EQ(candidate_manifest.at("artifacts").size(), focused_review.artifacts.size());

  const nlohmann::json* candidate_frame = FindPublishedCompleteFrame(candidate_manifest, 1.0);
  ASSERT_NE(candidate_frame, nullptr);
  const nlohmann::json& candidate_frame_metadata = candidate_frame->at("metadata");
  EXPECT_EQ(candidate_frame_metadata.at("transient_replacement_entity_ids"),
            nlohmann::json::array({1}));
  ASSERT_OK_AND_ASSIGN(
      const RgbaImage candidate_frame_image,
      ReadPng((candidate_output / candidate_frame->at("path").get<std::string>()).string()));
  const RgbaImage& expected_candidate = candidate.prepared.artwork.finished.image;
  int opaque_x = -1;
  int opaque_y = -1;
  for (int y = 0; y < expected_candidate.height && opaque_x < 0; ++y) {
    for (int x = 0; x < expected_candidate.width; ++x) {
      if (Pixel(expected_candidate, x, y).alpha == 0) continue;
      opaque_x = x;
      opaque_y = y;
      break;
    }
  }
  ASSERT_GE(opaque_x, 0);
  const SpriteFrame& candidate_frame_definition = candidate.prepared.sprite.frames.front();
  const nlohmann::json& camera_metadata = candidate_frame_metadata.at("camera");
  const double camera_x = camera_metadata.at("center").at("x").get<double>();
  const double camera_y = camera_metadata.at("center").at("y").get<double>();
  const double viewport_center_x =
      candidate_review_metadata.at("game_view").at("width").get<double>() / 2.0;
  const double viewport_center_y =
      candidate_review_metadata.at("game_view").at("height").get<double>() / 2.0;
  const int candidate_screen_x = static_cast<int>(
      std::lround(viewport_center_x + level.layers.front().entities.at(1).transform.position.x -
                  camera_x + candidate_frame_definition.offset_x + opaque_x));
  const int candidate_screen_y = static_cast<int>(
      std::lround(viewport_center_y + level.layers.front().entities.at(1).transform.position.y -
                  camera_y + candidate_frame_definition.offset_y + opaque_y));
  EXPECT_EQ(candidate_review_metadata.at("focus").at("bounds").at("min").at("x"),
            level.layers.front().entities.at(1).transform.position.x +
                candidate_frame_definition.offset_x);
  EXPECT_EQ(candidate_review_metadata.at("focus").at("bounds").at("min").at("y"),
            level.layers.front().entities.at(1).transform.position.y +
                candidate_frame_definition.offset_y);
  EXPECT_EQ(Pixel(candidate_frame_image, candidate_screen_x, candidate_screen_y),
            Pixel(expected_candidate, opaque_x, opaque_y));

  TemporaryReviewDirectory temporary;
  const std::filesystem::path output = temporary.path() / "published";
  ASSERT_OK_AND_ASSIGN(const size_t published_artifact_count,
                       reviewer.PublishReview(api, {.asset_id = level.id}, output.string()));
  EXPECT_EQ(published_artifact_count, review.artifacts.size());
  std::ifstream manifest_stream(output / "manifest.json");
  ASSERT_TRUE(manifest_stream.is_open());
  const nlohmann::json manifest = nlohmann::json::parse(manifest_stream);
  EXPECT_EQ(manifest.at("metadata"), review.metadata);
  ASSERT_EQ(manifest.at("artifacts").size(), review.artifacts.size());
  for (const nlohmann::json& published : manifest.at("artifacts")) {
    const auto original =
        std::find_if(review.artifacts.begin(), review.artifacts.end(),
                     [&published](const CurationArtifact& artifact) {
                       return artifact.id == published.at("id").get<std::string>();
                     });
    ASSERT_NE(original, review.artifacts.end());
    ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(original->image));
    EXPECT_EQ(published.at("rgba_sha256"), digest);
    EXPECT_EQ(published.at("metadata"), original->metadata);
  }
}

TEST(LevelReviewerTest, RejectsAnUnknownFocusedEntity) {
  Level level{
      .id = "level-id",
      .name = "Focused Level",
      .width = 1280,
      .height = 720,
      .spawn_point = {320, 360},
      .layers = {WorldLayer{.id = 0, .name = "Gameplay"}},
  };
  MockApi api;
  EXPECT_CALL(api, GetLevel(level.id)).WillOnce(Return(&level));

  LevelReviewer reviewer;
  EXPECT_EQ(
      reviewer.Review(api, {.asset_id = level.id, .focus_entity_id = uint64_t{99}}).status().code(),
      absl::StatusCode::kNotFound);
}

TEST(LevelReviewerTest, RejectsATransientCandidateWithoutAReplacementTarget) {
  Level level{
      .id = "level-id",
      .name = "Transient Candidate Level",
      .width = 1280,
      .height = 720,
      .spawn_point = {320, 360},
      .layers = {WorldLayer{.id = 0, .name = "Gameplay"}},
  };
  MockApi api;
  EXPECT_CALL(api, GetLevel(level.id)).WillOnce(Return(&level));

  LevelReviewer reviewer;
  EXPECT_TRUE(absl::IsInvalidArgument(
      reviewer.ReviewCandidate(api, {.asset_id = level.id}, nlohmann::json::object()).status()));
}

TEST(LevelReviewerTest, RejectsInactiveOrDuplicateFocusedEntitiesBeforeLoadingAssets) {
  Level inactive_level{
      .id = "inactive-level-id",
      .name = "Inactive Focused Level",
      .width = 1280,
      .height = 720,
      .spawn_point = {320, 360},
      .layers = {WorldLayer{.id = 0, .name = "Gameplay"}},
  };
  inactive_level.layers.front().entities.emplace(
      1, Entity{.id = 1, .active = false, .transform = {.position = {640, 360}}});
  MockApi inactive_api;
  EXPECT_CALL(inactive_api, GetLevel(inactive_level.id)).WillOnce(Return(&inactive_level));

  LevelReviewer reviewer;
  EXPECT_EQ(
      reviewer.Review(inactive_api, {.asset_id = inactive_level.id, .focus_entity_id = uint64_t{1}})
          .status()
          .code(),
      absl::StatusCode::kFailedPrecondition);

  Level duplicate_level{
      .id = "duplicate-level-id",
      .name = "Duplicate Focused Level",
      .width = 1280,
      .height = 720,
      .spawn_point = {320, 360},
      .layers = {WorldLayer{.id = 0, .name = "Gameplay"},
                 WorldLayer{.id = 1, .name = "Foreground"}},
  };
  for (WorldLayer& layer : duplicate_level.layers) {
    layer.entities.emplace(1, Entity{.id = 1, .transform = {.position = {640, 360}}});
  }
  MockApi duplicate_api;
  EXPECT_CALL(duplicate_api, GetLevel(duplicate_level.id)).WillOnce(Return(&duplicate_level));

  EXPECT_EQ(
      reviewer
          .Review(duplicate_api, {.asset_id = duplicate_level.id, .focus_entity_id = uint64_t{1}})
          .status()
          .code(),
      absl::StatusCode::kInvalidArgument);
}

TEST(LevelReviewerTest, RejectsAMissingReferencedTheme) {
  Level level{
      .id = "level-id",
      .name = "Broken Level",
      .width = 1280,
      .height = 720,
      .spawn_point = {320, 360},
      .zones = {{.id = 0,
                 .name = "Room",
                 .theme_id = "missing-theme",
                 .min_point = {0, 0},
                 .max_point = {1280, 720}}},
  };
  MockApi api;
  EXPECT_CALL(api, GetLevel(level.id)).WillOnce(Return(&level));
  EXPECT_CALL(api, GetParallaxTheme("missing-theme"))
      .WillOnce(Return(absl::NotFoundError("missing theme")));

  LevelReviewer reviewer;
  EXPECT_EQ(reviewer.Review(api, {.asset_id = level.id}).status().code(),
            absl::StatusCode::kNotFound);
}

TEST(LevelReviewerTest, RejectsAMissingReferencedSpriteTexture) {
  Level level{
      .id = "level-id",
      .name = "Broken Level",
      .width = 1280,
      .height = 720,
      .spawn_point = {320, 360},
      .layers = {WorldLayer{.id = 0, .name = "Decor"}},
  };
  level.layers.front().entities.emplace(
      1, Entity{.id = 1, .transform = {.position = {640, 360}}, .sprite_id = "sprite-id"});
  Sprite sprite{
      .id = "sprite-id",
      .name = "Broken Sprite",
      .texture_id = "missing-texture-id",
      .frames = {{.index = 0,
                  .texture_x = 0,
                  .texture_y = 0,
                  .texture_w = 1,
                  .texture_h = 1,
                  .render_w = 16,
                  .render_h = 16}},
  };
  MockApi api;
  EXPECT_CALL(api, GetLevel(level.id)).WillOnce(Return(&level));
  EXPECT_CALL(api, GetSprite(sprite.id)).WillOnce(Return(&sprite));
  EXPECT_CALL(api, GetTexture(sprite.texture_id))
      .WillOnce(Return(absl::NotFoundError("missing texture")));

  LevelReviewer reviewer;
  EXPECT_EQ(reviewer.Review(api, {.asset_id = level.id}).status().code(),
            absl::StatusCode::kNotFound);
}

TEST(LevelReviewerTest, RejectsAnArtifactSetOverThePixelBudgetBeforeRendering) {
  Level level{
      .id = "level-id",
      .name = "Oversized Review",
      .width = 1280,
      .height = 720,
      .spawn_point = {640, 360},
  };
  level.layers.clear();
  for (int index = 0; index < 400; ++index) {
    level.layers.push_back({.id = index, .name = absl::StrCat("Empty Layer ", index)});
  }
  MockApi api;
  EXPECT_CALL(api, GetLevel(level.id)).WillOnce(Return(&level));

  LevelReviewer reviewer;
  EXPECT_EQ(reviewer.Review(api, {.asset_id = level.id}).status().code(),
            absl::StatusCode::kResourceExhausted);
}

}  // namespace
}  // namespace zebes
