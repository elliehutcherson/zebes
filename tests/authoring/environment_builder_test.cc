#include "authoring/environment_builder.h"

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

std::filesystem::path ProductionSpecPath() {
  return std::filesystem::path(ZEBES_TEST_ASSETS_DIR) / "authoring" / "environments" /
         "catacombs_processional.json";
}

absl::StatusOr<nlohmann::json> ProductionSpecJson() {
  std::ifstream stream(ProductionSpecPath());
  if (!stream.is_open()) return absl::NotFoundError("could not open production environment spec");
  nlohmann::json json;
  stream >> json;
  return json;
}

TEST(EnvironmentBuilderTest, LoadsTheProductionCatacombsWithoutCatalogIds) {
  ASSERT_OK_AND_ASSIGN(const EnvironmentBuildSpec spec,
                       ReadEnvironmentBuildSpec(ProductionSpecPath()));

  EXPECT_EQ(spec.theme.layers.size(), 3);
  EXPECT_EQ(spec.theme.layers[0].elements.size(), 1);
  EXPECT_EQ(spec.theme.layers[1].elements.size(), 2);
  EXPECT_EQ(spec.theme.layers[2].elements.size(), 4);
  EXPECT_EQ(spec.level.columns, 512);
  EXPECT_EQ(spec.level.rows, 40);
  EXPECT_EQ(spec.level.tileset_name, "lucinda_cave");
  EXPECT_EQ(spec.level.terrain_name, "lucinda_cave");
}

TEST(EnvironmentBuilderTest, RejectsUnknownFieldsInsteadOfSilentlyIgnoringThem) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["theme"]["unrecognized"] = true;

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EnvironmentBuilderTest, RejectsTerrainOutsideTheDeclaredGrid) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["level"]["terrain_rectangles"][0]["width"] = 513;

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EnvironmentBuilderTest, RejectsUnsupportedSchemaVersions) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["schema_version"] = 2;

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
