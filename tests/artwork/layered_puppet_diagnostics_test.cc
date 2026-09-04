#include "artwork/layered_puppet_diagnostics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "gtest/gtest.h"

namespace zebes {
namespace {

constexpr int kSize = 12;

RgbaImage BlankImage() {
  return RgbaImage{
      .width = kSize,
      .height = kSize,
      .pixels = std::vector<uint8_t>(kSize * kSize * 4, 0),
  };
}

void Fill(RgbaImage& image, int left, int top, int right, int bottom) {
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      image.pixels[offset] = 90;
      image.pixels[offset + 1] = 140;
      image.pixels[offset + 2] = 70;
      image.pixels[offset + 3] = 255;
    }
  }
}

// A unit square split into two triangles, wound counter-clockwise in a
// y-down canvas.
LayeredPuppetMesh UnitSquareMesh() {
  LayeredPuppetMesh mesh;
  mesh.vertices = {
      {.source = {.x = 0.0, .y = 0.0}},
      {.source = {.x = 1.0, .y = 0.0}},
      {.source = {.x = 0.0, .y = 1.0}},
      {.source = {.x = 1.0, .y = 1.0}},
  };
  mesh.triangles = {{.vertices = {0, 1, 2}}, {.vertices = {1, 3, 2}}};
  return mesh;
}

// Covers the unit-square mesh, so every triangle in it counts as over artwork.
RgbaImage OpaqueUnitSquare() {
  RgbaImage image = BlankImage();
  Fill(image, 0, 0, 2, 2);
  return image;
}

std::vector<ProfileControlPoint> RestVertices(const LayeredPuppetMesh& mesh) {
  std::vector<ProfileControlPoint> points;
  points.reserve(mesh.vertices.size());
  for (const LayeredPuppetMeshVertex& vertex : mesh.vertices) points.push_back(vertex.source);
  return points;
}

TEST(LayeredPuppetDiagnosticsTest, RestMeshHasNoInvertedTriangles) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  const absl::StatusOr<LayeredPuppetTriangleReport> report =
      MeasureLayeredPuppetTriangles(mesh, RestVertices(mesh), OpaqueUnitSquare());
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->triangles, 2u);
  EXPECT_EQ(report->inverted, 0u);
  EXPECT_EQ(report->degenerate, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, RigidRotationDoesNotInvertTriangles) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  std::vector<ProfileControlPoint> rotated;
  for (const LayeredPuppetMeshVertex& vertex : mesh.vertices) {
    rotated.push_back({.x = -vertex.source.y, .y = vertex.source.x});
  }
  const absl::StatusOr<LayeredPuppetTriangleReport> report =
      MeasureLayeredPuppetTriangles(mesh, rotated, OpaqueUnitSquare());
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->inverted, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, MirroredVerticesInvertEveryTriangle) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  std::vector<ProfileControlPoint> mirrored;
  for (const LayeredPuppetMeshVertex& vertex : mesh.vertices) {
    mirrored.push_back({.x = -vertex.source.x, .y = vertex.source.y});
  }
  const absl::StatusOr<LayeredPuppetTriangleReport> report =
      MeasureLayeredPuppetTriangles(mesh, mirrored, OpaqueUnitSquare());
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->inverted, 2u);
  EXPECT_EQ(report->inverted_over_artwork, 2u);
  EXPECT_EQ(report->degenerate, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, FoldOverTransparentArtworkCostsNothing) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  std::vector<ProfileControlPoint> mirrored;
  for (const LayeredPuppetMeshVertex& vertex : mesh.vertices) {
    mirrored.push_back({.x = -vertex.source.x, .y = vertex.source.y});
  }
  RgbaImage elsewhere = BlankImage();
  Fill(elsewhere, 8, 8, 10, 10);
  const absl::StatusOr<LayeredPuppetTriangleReport> report =
      MeasureLayeredPuppetTriangles(mesh, mirrored, elsewhere);
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->inverted, 2u);
  EXPECT_EQ(report->inverted_over_artwork, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, TriangleReportRejectsInvalidArtwork) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  EXPECT_FALSE(MeasureLayeredPuppetTriangles(mesh, RestVertices(mesh), RgbaImage{}).ok());
}

TEST(LayeredPuppetDiagnosticsTest, CollapsedVerticesCountAsDegenerate) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  std::vector<ProfileControlPoint> collapsed(mesh.vertices.size(), {.x = 3.0, .y = 4.0});
  const absl::StatusOr<LayeredPuppetTriangleReport> report =
      MeasureLayeredPuppetTriangles(mesh, collapsed, OpaqueUnitSquare());
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->degenerate, 2u);
  EXPECT_EQ(report->inverted, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, TriangleReportRejectsMismatchedVertexCount) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  std::vector<ProfileControlPoint> short_input = RestVertices(mesh);
  short_input.pop_back();
  EXPECT_FALSE(MeasureLayeredPuppetTriangles(mesh, short_input, OpaqueUnitSquare()).ok());
}

TEST(LayeredPuppetDiagnosticsTest, TriangleReportRejectsNonFiniteVertices) {
  const LayeredPuppetMesh mesh = UnitSquareMesh();
  std::vector<ProfileControlPoint> broken = RestVertices(mesh);
  broken[1].x = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(MeasureLayeredPuppetTriangles(mesh, broken, OpaqueUnitSquare()).ok());
}

TEST(LayeredPuppetDiagnosticsTest, TriangleReportRejectsZeroAreaRestTriangle) {
  LayeredPuppetMesh mesh = UnitSquareMesh();
  mesh.vertices[2].source = {.x = 2.0, .y = 0.0};
  EXPECT_FALSE(MeasureLayeredPuppetTriangles(mesh, RestVertices(mesh), OpaqueUnitSquare()).ok());
}

TEST(LayeredPuppetDiagnosticsTest, TriangleReportRejectsEmptyMesh) {
  EXPECT_FALSE(MeasureLayeredPuppetTriangles(LayeredPuppetMesh{}, {}, OpaqueUnitSquare()).ok());
}

TEST(LayeredPuppetDiagnosticsTest, FullyBackedMovingLayerLeavesNothingUncovered) {
  RgbaImage moving = BlankImage();
  Fill(moving, 4, 4, 8, 8);
  RgbaImage backing = BlankImage();
  Fill(backing, 2, 2, 10, 10);
  const std::array<RgbaImage, 1> layers = {backing};
  const absl::StatusOr<LayeredPuppetBackfillReport> report =
      MeasureLayeredPuppetBackfill(moving, layers);
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->moving_pixels, 16u);
  EXPECT_EQ(report->uncovered_pixels, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, PartlyBackedMovingLayerCountsTheGap) {
  RgbaImage moving = BlankImage();
  Fill(moving, 4, 4, 8, 8);
  RgbaImage backing = BlankImage();
  Fill(backing, 4, 4, 6, 8);
  const std::array<RgbaImage, 1> layers = {backing};
  const absl::StatusOr<LayeredPuppetBackfillReport> report =
      MeasureLayeredPuppetBackfill(moving, layers);
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->uncovered_pixels, 8u);
}

TEST(LayeredPuppetDiagnosticsTest, BackfillCombinesEveryStaticLayer) {
  RgbaImage moving = BlankImage();
  Fill(moving, 4, 4, 8, 8);
  RgbaImage left = BlankImage();
  Fill(left, 4, 4, 6, 8);
  RgbaImage right = BlankImage();
  Fill(right, 6, 4, 8, 8);
  const std::array<RgbaImage, 2> layers = {left, right};
  const absl::StatusOr<LayeredPuppetBackfillReport> report =
      MeasureLayeredPuppetBackfill(moving, layers);
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->uncovered_pixels, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, BackfillRejectsMismatchedCanvases) {
  RgbaImage moving = BlankImage();
  Fill(moving, 4, 4, 8, 8);
  RgbaImage backing{
      .width = 4,
      .height = 4,
      .pixels = std::vector<uint8_t>(4 * 4 * 4, 255),
  };
  const std::array<RgbaImage, 1> layers = {backing};
  EXPECT_FALSE(MeasureLayeredPuppetBackfill(moving, layers).ok());
}

TEST(LayeredPuppetDiagnosticsTest, BackfillRejectsAnEmptyMovingLayer) {
  const RgbaImage moving = BlankImage();
  RgbaImage backing = BlankImage();
  Fill(backing, 2, 2, 10, 10);
  const std::array<RgbaImage, 1> layers = {backing};
  EXPECT_FALSE(MeasureLayeredPuppetBackfill(moving, layers).ok());
}

TEST(LayeredPuppetDiagnosticsTest, BackfillRejectsNoStaticLayers) {
  RgbaImage moving = BlankImage();
  Fill(moving, 4, 4, 8, 8);
  EXPECT_FALSE(MeasureLayeredPuppetBackfill(moving, {}).ok());
}

TEST(LayeredPuppetDiagnosticsTest, CleanStaticLayerReportsNoOrphans) {
  RgbaImage body = BlankImage();
  Fill(body, 1, 1, 5, 11);
  RgbaImage arm = BlankImage();
  Fill(arm, 6, 3, 10, 6);
  const absl::StatusOr<LayeredPuppetOrphanReport> report = MeasureLayeredPuppetOrphans(body, arm);
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->components, 0u);
  EXPECT_EQ(report->orphan_pixels, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, IslandInsideTheMovingFootprintIsAnOrphan) {
  RgbaImage body = BlankImage();
  Fill(body, 1, 1, 5, 11);
  Fill(body, 7, 4, 9, 5);
  RgbaImage arm = BlankImage();
  Fill(arm, 6, 3, 10, 6);
  const absl::StatusOr<LayeredPuppetOrphanReport> report = MeasureLayeredPuppetOrphans(body, arm);
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->components, 1u);
  EXPECT_EQ(report->orphan_pixels, 2u);
}

TEST(LayeredPuppetDiagnosticsTest, IslandOutsideTheMovingFootprintIsLegitimateArtwork) {
  RgbaImage body = BlankImage();
  Fill(body, 1, 1, 5, 11);
  Fill(body, 0, 0, 1, 1);
  RgbaImage arm = BlankImage();
  Fill(arm, 6, 3, 10, 6);
  const absl::StatusOr<LayeredPuppetOrphanReport> report = MeasureLayeredPuppetOrphans(body, arm);
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->components, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, OrphanReportRejectsAnEmptyMovingLayer) {
  RgbaImage body = BlankImage();
  Fill(body, 1, 1, 5, 11);
  EXPECT_FALSE(MeasureLayeredPuppetOrphans(body, BlankImage()).ok());
}

TEST(LayeredPuppetDiagnosticsTest, OrphanReportRejectsAnEmptyStaticLayer) {
  RgbaImage arm = BlankImage();
  Fill(arm, 6, 3, 10, 6);
  EXPECT_FALSE(MeasureLayeredPuppetOrphans(BlankImage(), arm).ok());
}

TEST(LayeredPuppetDiagnosticsTest, SolidShapeHasNoInteriorHoles) {
  RgbaImage pose = BlankImage();
  Fill(pose, 3, 3, 9, 9);
  const absl::StatusOr<size_t> holes = MeasureLayeredPuppetInteriorHoles(pose);
  ASSERT_TRUE(holes.ok()) << holes.status();
  EXPECT_EQ(*holes, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, EnclosedGapCountsAsAnInteriorHole) {
  RgbaImage pose = BlankImage();
  Fill(pose, 3, 3, 9, 9);
  for (int y = 5; y < 7; ++y) {
    for (int x = 5; x < 7; ++x) {
      pose.pixels[(static_cast<size_t>(y) * pose.width + x) * 4 + 3] = 0;
    }
  }
  const absl::StatusOr<size_t> holes = MeasureLayeredPuppetInteriorHoles(pose);
  ASSERT_TRUE(holes.ok()) << holes.status();
  EXPECT_EQ(*holes, 4u);
}

TEST(LayeredPuppetDiagnosticsTest, NotchOpenToTheEdgeIsNotAnInteriorHole) {
  RgbaImage pose = BlankImage();
  Fill(pose, 3, 3, 9, 9);
  for (int y = 3; y < 6; ++y) {
    for (int x = 7; x < 9; ++x) {
      pose.pixels[(static_cast<size_t>(y) * pose.width + x) * 4 + 3] = 0;
    }
  }
  const absl::StatusOr<size_t> holes = MeasureLayeredPuppetInteriorHoles(pose);
  ASSERT_TRUE(holes.ok()) << holes.status();
  EXPECT_EQ(*holes, 0u);
}

TEST(LayeredPuppetDiagnosticsTest, InteriorHoleReportRejectsAnInvalidImage) {
  EXPECT_FALSE(MeasureLayeredPuppetInteriorHoles(RgbaImage{}).ok());
}

}  // namespace
}  // namespace zebes
