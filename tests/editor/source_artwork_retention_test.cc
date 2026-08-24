#include "editor/source_artwork_retention.h"

#include <string>

#include "absl/status/status.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/api_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

RgbaImage Pixels() { return RgbaImage{.width = 1, .height = 1, .pixels = {10, 20, 30, 255}}; }

SourceArtwork Source() {
  return SourceArtwork{
      .id = "source-1",
      .name = "Generated source",
      .source_path = "source_art/source-1.png",
      .provenance =
          ImportedArtworkProvenance{
              .original_filename = "source.png",
              .imported_at_utc = "2026-08-24T12:00:00Z",
          },
      .width = 1,
      .height = 1,
      .content_digest = std::string(64, 'a'),
  };
}

TEST(SourceArtworkRetentionTest, ReturnsOnlyAfterTheRetainedSourceIsAccepted) {
  NiceMock<MockApi> api;
  SourceArtwork source = Source();
  EXPECT_CALL(api, CreateSourceArtwork(StrEq("Generated source"), _, _))
      .WillOnce(Return(std::string(source.id)));
  EXPECT_CALL(api, GetSourceArtwork(StrEq(source.id))).WillOnce(Return(&source));
  EXPECT_CALL(api, DeleteSourceArtwork(_)).Times(0);
  bool accepted = false;

  ASSERT_OK_AND_ASSIGN(const std::string id,
                       RetainSourceArtwork(api, source.name, source.provenance, Pixels(),
                                           [&accepted](const SourceArtwork&, const RgbaImage&) {
                                             accepted = true;
                                             return absl::OkStatus();
                                           }));

  EXPECT_EQ(id, source.id);
  EXPECT_TRUE(accepted);
}

TEST(SourceArtworkRetentionTest, RejectsMissingAcceptanceCallbackBeforeCreatingSource) {
  NiceMock<MockApi> api;
  EXPECT_CALL(api, CreateSourceArtwork(_, _, _)).Times(0);

  const absl::Status status =
      RetainSourceArtwork(api, "Generated source", Source().provenance, Pixels(), {}).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SourceArtworkRetentionTest, DeletesTheNewSourceWhenReloadFails) {
  NiceMock<MockApi> api;
  EXPECT_CALL(api, CreateSourceArtwork(_, _, _)).WillOnce(Return(std::string("source-1")));
  EXPECT_CALL(api, GetSourceArtwork(StrEq("source-1")))
      .WillOnce(Return(absl::NotFoundError("source definition missing")));
  EXPECT_CALL(api, DeleteSourceArtwork(StrEq("source-1"))).WillOnce(Return(absl::OkStatus()));

  const absl::Status status =
      RetainSourceArtwork(api, "Generated source", Source().provenance, Pixels(),
                          [](const SourceArtwork&, const RgbaImage&) { return absl::OkStatus(); })
          .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST(SourceArtworkRetentionTest, DeletesTheNewSourceWhenDomainAcceptanceFails) {
  NiceMock<MockApi> api;
  SourceArtwork source = Source();
  EXPECT_CALL(api, CreateSourceArtwork(_, _, _)).WillOnce(Return(std::string(source.id)));
  EXPECT_CALL(api, GetSourceArtwork(StrEq(source.id))).WillOnce(Return(&source));
  EXPECT_CALL(api, DeleteSourceArtwork(StrEq(source.id))).WillOnce(Return(absl::OkStatus()));

  const absl::Status status =
      RetainSourceArtwork(api, source.name, source.provenance, Pixels(),
                          [](const SourceArtwork&, const RgbaImage&) {
                            return absl::FailedPreconditionError("editor rejected source");
                          })
          .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
