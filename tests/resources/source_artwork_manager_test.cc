#include "resources/source_artwork_manager.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "artwork/source_artwork.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

using ::testing::HasSubstr;

RgbaImage TestImage() {
  return RgbaImage{
      .width = 2,
      .height = 2,
      .pixels = {10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 0, 0, 0, 0},
  };
}

RgbaImage LargerTestImage() {
  RgbaImage image{.width = 32, .height = 32};
  image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);
  uint32_t state = 0x12345678;
  for (uint8_t& byte : image.pixels) {
    state = state * 1664525 + 1013904223;
    byte = static_cast<uint8_t>(state >> 24);
  }
  return image;
}

class SourceArtworkManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("zebes-source-artwork-" + std::to_string(++sequence));
    std::filesystem::remove_all(path_);
    ASSERT_OK_AND_ASSIGN(manager_, SourceArtworkManager::Create(path_.string()));
    ASSERT_OK(manager_->LoadAllArtwork());
  }

  void TearDown() override { std::filesystem::remove_all(path_); }

  std::filesystem::path path_;
  std::unique_ptr<SourceArtworkManager> manager_;
  static int sequence;
};

int SourceArtworkManagerTest::sequence = 0;

TEST_F(SourceArtworkManagerTest, CreatesAndReloadsAnImportedSourceFromCanonicalPixels) {
  const RgbaImage image = TestImage();
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              image));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllArtwork());
  ASSERT_OK_AND_ASSIGN(SourceArtwork * artwork, reloaded->GetArtwork(id));
  EXPECT_EQ(artwork->source_path, "source_art/" + id + ".png");
  EXPECT_EQ(artwork->width, image.width);
  EXPECT_EQ(artwork->height, image.height);
  EXPECT_EQ(artwork->content_digest.size(), 64u);
  ASSERT_TRUE(std::holds_alternative<ImportedArtworkProvenance>(artwork->provenance));

  ASSERT_OK_AND_ASSIGN(const RgbaImage decoded, reloaded->ReadArtworkPixels(id));
  EXPECT_EQ(decoded.pixels, image.pixels);
}

TEST_F(SourceArtworkManagerTest, GeneratedProvenanceWritesExplicitNulls) {
  ASSERT_OK_AND_ASSIGN(const std::string id,
                       manager_->CreateArtwork("Generated tree",
                                               GeneratedArtworkProvenance{
                                                   .provider = "provider",
                                                   .model = "model-v1",
                                                   .submitted_prompt = "one tree",
                                                   .generated_at_utc = "2026-08-16T15:04:05Z",
                                               },
                                               TestImage()));

  std::ifstream stream(path_ / "definitions/source_artworks" / (id + ".json"));
  nlohmann::json json;
  stream >> json;
  EXPECT_TRUE(json.at("provenance").at("revised_prompt").is_null());
  EXPECT_TRUE(json.at("provenance").at("provider_request_id").is_null());
}

TEST_F(SourceArtworkManagerTest, ReplacesPixelsAndProvenanceWithoutChangingIdentity) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  ASSERT_OK_AND_ASSIGN(SourceArtwork * current, manager_->GetArtwork(id));
  const SourceArtwork snapshot = *current;
  const RgbaImage replacement_pixels{
      .width = 3,
      .height = 1,
      .pixels = {1, 2, 3, 255, 4, 5, 6, 128, 7, 8, 9, 0},
  };
  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(replacement_pixels));
  SourceArtwork replacement = snapshot;
  replacement.provenance = GeneratedArtworkProvenance{
      .provider = "imagegen",
      .model = "builtin",
      .submitted_prompt = "redraw the edges",
      .generated_at_utc = "2026-08-27T22:00:00Z",
  };
  replacement.width = replacement_pixels.width;
  replacement.height = replacement_pixels.height;
  replacement.content_digest = digest;

  ASSERT_OK(manager_->ReplaceArtwork(snapshot, replacement, replacement_pixels));
  ASSERT_OK_AND_ASSIGN(SourceArtwork * updated, manager_->GetArtwork(id));
  EXPECT_EQ(updated->id, snapshot.id);
  EXPECT_EQ(updated->name, snapshot.name);
  EXPECT_EQ(updated->source_path, snapshot.source_path);
  EXPECT_EQ(updated->content_digest, digest);
  ASSERT_OK_AND_ASSIGN(const RgbaImage decoded, manager_->ReadArtworkPixels(id));
  EXPECT_EQ(decoded.pixels, replacement_pixels.pixels);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  ASSERT_OK(reloaded->LoadAllArtwork());
  ASSERT_OK_AND_ASSIGN(SourceArtwork * reloaded_artwork, reloaded->GetArtwork(id));
  EXPECT_EQ(SourceArtworkToJson(*reloaded_artwork), SourceArtworkToJson(replacement));
}

TEST_F(SourceArtworkManagerTest, RefusesAStaleReplacementBeforeWriting) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  ASSERT_OK_AND_ASSIGN(SourceArtwork * current, manager_->GetArtwork(id));
  SourceArtwork stale = *current;
  stale.content_digest = std::string(64, '0');
  const absl::Status status = manager_->ReplaceArtwork(stale, *current, TestImage());
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  ASSERT_OK_AND_ASSIGN(const RgbaImage decoded, manager_->ReadArtworkPixels(id));
  EXPECT_EQ(decoded.pixels, TestImage().pixels);
}

TEST_F(SourceArtworkManagerTest, RefusesReplacementIdentityChanges) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  ASSERT_OK_AND_ASSIGN(SourceArtwork * current, manager_->GetArtwork(id));
  const SourceArtwork snapshot = *current;
  SourceArtwork replacement = snapshot;
  replacement.name = "Different tree";
  const absl::Status status = manager_->ReplaceArtwork(snapshot, replacement, TestImage());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(SourceArtworkManagerTest, RejectsPixelsChangedAfterAcceptance) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  const RgbaImage replacement{
      .width = 2,
      .height = 2,
      .pixels = {1, 2, 3, 255, 1, 2, 3, 255, 1, 2, 3, 255, 1, 2, 3, 255},
  };
  ASSERT_OK(WritePng((path_ / "source_art" / (id + ".png")).string(), replacement.width,
                     replacement.height, replacement.pixels));

  const absl::Status read_status = manager_->ReadArtworkPixels(id).status();
  EXPECT_EQ(read_status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(read_status.message()), HasSubstr("digest"));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllArtwork();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("digest"));
}

TEST_F(SourceArtworkManagerTest, RejectsMetadataChangedAfterAcceptance) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  ASSERT_OK_AND_ASSIGN(SourceArtwork * artwork, manager_->GetArtwork(id));
  ++artwork->width;

  const absl::Status status = manager_->ReadArtworkPixels(id).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("dimensions"));
}

TEST_F(SourceArtworkManagerTest, RejectsFilesOverTheManagerEncodedByteLimitBeforeReading) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  const std::filesystem::path image_path = path_ / "source_art" / (id + ".png");
  std::filesystem::resize_file(image_path, SourceArtworkLimits{}.maximum_encoded_bytes + 1);

  const absl::Status read_status = manager_->ReadArtworkPixels(id).status();
  EXPECT_EQ(read_status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_THAT(std::string(read_status.message()), HasSubstr("encoded image"));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  const absl::Status load_status = reloaded->LoadAllArtwork();
  EXPECT_EQ(load_status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(load_status.message()), HasSubstr("encoded image"));
}

TEST_F(SourceArtworkManagerTest, CreateRejectsAnEncodedPngOverItsDistinctLimitAndCleansUp) {
  ASSERT_OK_AND_ASSIGN(const std::vector<uint8_t> encoded, EncodePng(TestImage()));
  SourceArtworkLimits limits;
  limits.maximum_encoded_bytes = encoded.size() - 1;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> limited,
                       SourceArtworkManager::Create(path_.string(), limits));

  const absl::Status status =
      limited
          ->CreateArtwork("Tree source",
                          ImportedArtworkProvenance{.original_filename = "oak.png",
                                                    .imported_at_utc = "2026-08-16T15:04:05Z"},
                          TestImage())
          .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_TRUE(std::filesystem::is_empty(path_ / "definitions/source_artworks"));
  EXPECT_TRUE(std::filesystem::is_empty(path_ / "source_art"));
}

TEST_F(SourceArtworkManagerTest, ReplaceRejectsAnEncodedPngOverItsDistinctLimitAndCleansUp) {
  ASSERT_OK_AND_ASSIGN(const std::vector<uint8_t> encoded, EncodePng(TestImage()));
  SourceArtworkLimits limits;
  limits.maximum_encoded_bytes = encoded.size();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> limited,
                       SourceArtworkManager::Create(path_.string(), limits));
  ASSERT_OK(limited->LoadAllArtwork());
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      limited->CreateArtwork("Tree source",
                             ImportedArtworkProvenance{.original_filename = "oak.png",
                                                       .imported_at_utc = "2026-08-16T15:04:05Z"},
                             TestImage()));
  ASSERT_OK_AND_ASSIGN(SourceArtwork * current, limited->GetArtwork(id));
  const SourceArtwork snapshot = *current;
  const RgbaImage replacement_pixels = LargerTestImage();
  ASSERT_OK_AND_ASSIGN(const std::vector<uint8_t> replacement_encoded,
                       EncodePng(replacement_pixels));
  ASSERT_GT(replacement_encoded.size(), limits.maximum_encoded_bytes);
  ASSERT_OK_AND_ASSIGN(const std::string replacement_digest, RgbaImageDigest(replacement_pixels));
  SourceArtwork replacement = snapshot;
  replacement.width = replacement_pixels.width;
  replacement.height = replacement_pixels.height;
  replacement.content_digest = replacement_digest;

  const absl::Status status = limited->ReplaceArtwork(snapshot, replacement, replacement_pixels);
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  ASSERT_OK_AND_ASSIGN(SourceArtwork * unchanged, limited->GetArtwork(id));
  EXPECT_EQ(SourceArtworkToJson(*unchanged), SourceArtworkToJson(snapshot));
  ASSERT_OK_AND_ASSIGN(const RgbaImage retained, limited->ReadArtworkPixels(id));
  EXPECT_EQ(retained.pixels, TestImage().pixels);
  EXPECT_FALSE(std::filesystem::exists(path_ / "source_art" / (id + ".png.replacing")));
  EXPECT_FALSE(std::filesystem::exists(path_ / "source_art" / (id + ".png.replaced")));
}

TEST_F(SourceArtworkManagerTest, RejectsAZeroEncodedByteLimitAtCreation) {
  SourceArtworkLimits limits;
  limits.maximum_encoded_bytes = 0;
  const absl::Status status = SourceArtworkManager::Create(path_.string(), limits).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("maximum encoded bytes"));
}

TEST_F(SourceArtworkManagerTest, RejectsDecodedByteLimitSmallerThanOnePixel) {
  SourceArtworkLimits limits;
  limits.maximum_bytes = 3;
  const absl::Status status = SourceArtworkManager::Create(path_.string(), limits).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("one RGBA8 pixel"));
}

TEST_F(SourceArtworkManagerTest, BoundedReadRejectsInvalidAndBroaderPixelLimits) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));

  EXPECT_EQ(manager_->ReadArtworkPixels(id, 0).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      manager_->ReadArtworkPixels(id, SourceArtworkLimits{}.maximum_pixels + 1).status().code(),
      absl::StatusCode::kInvalidArgument);
}

TEST_F(SourceArtworkManagerTest, BoundedReadRejectsDecodedPixelsOverThePerCallLimit) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));

  const absl::Status status = manager_->ReadArtworkPixels(id, 3).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_THAT(std::string(status.message()), HasSubstr("3 pixel limit"));
}

TEST_F(SourceArtworkManagerTest, LoadRejectsDecodedPixelsOverTheManagerPixelLimit) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  static_cast<void>(id);

  SourceArtworkLimits limits;
  limits.maximum_pixels = 3;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> limited,
                       SourceArtworkManager::Create(path_.string(), limits));
  const absl::Status status = limited->LoadAllArtwork();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("3 pixel limit"));
}

TEST_F(SourceArtworkManagerTest, LoadRejectsDimensionsOverTheManagerLimit) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  static_cast<void>(id);

  SourceArtworkLimits limits;
  limits.maximum_width = 1;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> limited,
                       SourceArtworkManager::Create(path_.string(), limits));
  const absl::Status status = limited->LoadAllArtwork();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("exceeds configured limits"));
}

TEST_F(SourceArtworkManagerTest, RejectsAPathOutsideTheIdBackedSourceDirectory) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  const std::filesystem::path definition = path_ / "definitions/source_artworks" / (id + ".json");
  std::ifstream input(definition);
  nlohmann::json json;
  input >> json;
  input.close();
  json["source_path"] = "../outside.png";
  std::ofstream(definition, std::ios::trunc) << json.dump(2);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<SourceArtworkManager> reloaded,
                       SourceArtworkManager::Create(path_.string()));
  const absl::Status status = reloaded->LoadAllArtwork();
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("ID-backed"));
}

TEST_F(SourceArtworkManagerTest, EnforcesLimitsBeforeWritingAnything) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<SourceArtworkManager> limited,
      SourceArtworkManager::Create(path_.string(), SourceArtworkLimits{.maximum_width = 1}));
  const absl::Status status =
      limited
          ->CreateArtwork("Too large",
                          ImportedArtworkProvenance{.original_filename = "oak.png",
                                                    .imported_at_utc = "2026-08-16T15:04:05Z"},
                          TestImage())
          .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_TRUE(std::filesystem::is_empty(path_ / "definitions/source_artworks"));
}

TEST_F(SourceArtworkManagerTest, DeleteRemovesDefinitionAndRetainedPixels) {
  ASSERT_OK_AND_ASSIGN(
      const std::string id,
      manager_->CreateArtwork("Tree source",
                              ImportedArtworkProvenance{.original_filename = "oak.png",
                                                        .imported_at_utc = "2026-08-16T15:04:05Z"},
                              TestImage()));
  ASSERT_OK(manager_->DeleteArtwork(id));

  EXPECT_FALSE(std::filesystem::exists(path_ / "definitions/source_artworks" / (id + ".json")));
  EXPECT_FALSE(std::filesystem::exists(path_ / "source_art" / (id + ".png")));
  EXPECT_EQ(manager_->GetArtwork(id).status().code(), absl::StatusCode::kNotFound);
}

}  // namespace
}  // namespace zebes
