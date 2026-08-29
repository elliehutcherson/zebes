#include "curation/generated_asset_quarantine.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "api_mock.h"
#include "artwork/parallax_artwork_recipe.h"
#include "artwork/prop_recipe.h"
#include "artwork/source_artwork.h"
#include "common/utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"
#include "objects/blueprint.h"
#include "objects/sprite.h"
#include "objects/texture.h"
#include "objects/tileset.h"
#include "terrain/terrain_recipe.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;

class TemporaryAssetTree {
 public:
  TemporaryAssetTree()
      : root_(std::filesystem::temp_directory_path() / ("zebes-quarantine-test-" + GenerateGuid())),
        assets_(root_ / "assets"),
        output_(root_ / "quarantine") {
    std::filesystem::create_directories(assets_);
  }

  ~TemporaryAssetTree() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  void Write(const std::filesystem::path& relative_path, std::string_view contents = "fixture") {
    const std::filesystem::path path = assets_ / relative_path;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
  }

  const std::filesystem::path& assets() const { return assets_; }
  const std::filesystem::path& output() const { return output_; }

 private:
  std::filesystem::path root_;
  std::filesystem::path assets_;
  std::filesystem::path output_;
};

nlohmann::json ReadManifest(const TemporaryAssetTree& tree) {
  std::ifstream stream(tree.output() / "manifest.json");
  nlohmann::json manifest;
  stream >> manifest;
  return manifest;
}

void WriteTextureFiles(TemporaryAssetTree& tree, const Texture& texture) {
  tree.Write(std::filesystem::path("definitions/textures") /
             (texture.name + "-" + texture.id + ".json"));
  const std::filesystem::path image_path = texture.path.starts_with("textures/")
                                               ? std::filesystem::path(texture.path)
                                               : std::filesystem::path("textures") / texture.path;
  tree.Write(image_path, "png");
}

void WriteSourceFiles(TemporaryAssetTree& tree, const SourceArtwork& source) {
  tree.Write(std::filesystem::path("definitions/source_artworks") / (source.id + ".json"));
  tree.Write(source.source_path, "png");
}

GeneratedAssetQuarantineOptions Options(const TemporaryAssetTree& tree, GeneratedAssetKind kind,
                                        std::string recipe_id) {
  return {
      .asset_root = tree.assets().string(),
      .output_path = tree.output().string(),
      .kind = kind,
      .recipe_id = std::move(recipe_id),
  };
}

TEST(GeneratedAssetQuarantineTest, RejectsReferencedPropBeforePublishingAnything) {
  TemporaryAssetTree tree;
  NiceMock<MockApi> api;
  EXPECT_CALL(api, CheckGeneratedPropDeletable("recipe"))
      .WillOnce(Return(absl::FailedPreconditionError("level uses prop")));
  EXPECT_CALL(api, GetPropRecipe(_)).Times(0);
  EXPECT_CALL(api, DeleteGeneratedProp(_)).Times(0);

  const absl::Status status =
      QuarantineGeneratedAsset(api, Options(tree, GeneratedAssetKind::kProp, "recipe"));

  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(std::string(status.message()), HasSubstr("level uses prop"));
  EXPECT_FALSE(std::filesystem::exists(tree.output()));
}

TEST(GeneratedAssetQuarantineTest, PublishesCompletePropRecoveryBeforeDeleting) {
  TemporaryAssetTree tree;
  NiceMock<MockApi> api;
  PropRecipe recipe{
      .id = "recipe",
      .name = "Cave Rock",
      .source_artwork_id = "source",
      .texture_id = "texture",
      .sprite_id = "sprite",
      .blueprint_id = "blueprint",
  };
  Blueprint blueprint{
      .id = "blueprint",
      .name = "Cave Rock",
      .states = {{.name = "Default", .sprite_id = "sprite"}},
  };
  Sprite sprite{.id = "sprite", .name = "Cave Rock", .texture_id = "texture"};
  Texture texture{
      .id = "texture", .name = "Cave Rock", .path = "textures/prop_artwork/texture.png"};
  SourceArtwork source{
      .id = "source",
      .name = "Cave Rock source",
      .source_path = "source_art/source.png",
  };
  tree.Write("definitions/prop_recipes/recipe.json");
  tree.Write("definitions/blueprints/Cave Rock-blueprint.json");
  tree.Write("definitions/sprites/Cave Rock-sprite.json");
  WriteTextureFiles(tree, texture);
  WriteSourceFiles(tree, source);

  EXPECT_CALL(api, CheckGeneratedPropDeletable("recipe")).WillOnce(Return(absl::OkStatus()));
  ON_CALL(api, GetPropRecipe("recipe")).WillByDefault(Return(&recipe));
  ON_CALL(api, GetBlueprint("blueprint")).WillByDefault(Return(&blueprint));
  ON_CALL(api, GetSprite("sprite")).WillByDefault(Return(&sprite));
  ON_CALL(api, GetTexture("texture")).WillByDefault(Return(&texture));
  ON_CALL(api, GetSourceArtwork("source")).WillByDefault(Return(&source));
  ON_CALL(api, GetAllPropRecipes()).WillByDefault(Return(std::vector<PropRecipe>{recipe}));
  ON_CALL(api, GetAllParallaxArtworkRecipes())
      .WillByDefault(Return(std::vector<ParallaxArtworkRecipe>{}));
  EXPECT_CALL(api, DeleteGeneratedProp("recipe")).WillOnce([&tree](const std::string&) {
    EXPECT_TRUE(std::filesystem::exists(tree.output() / "manifest.json"));
    return absl::OkStatus();
  });

  EXPECT_OK(QuarantineGeneratedAsset(api, Options(tree, GeneratedAssetKind::kProp, "recipe")));
  const nlohmann::json manifest = ReadManifest(tree);
  EXPECT_EQ(manifest.at("asset_kind"), "prop");
  EXPECT_EQ(manifest.at("files").size(), 7);
  for (const nlohmann::json& file : manifest.at("files")) {
    EXPECT_EQ(file.at("live_disposition"), "removed");
    EXPECT_TRUE(
        std::filesystem::exists(tree.output() / "assets" / file.at("path").get<std::string>()));
  }
}

TEST(GeneratedAssetQuarantineTest, RetainsSharedSourceAndReportsFailedLiveRemoval) {
  TemporaryAssetTree tree;
  NiceMock<MockApi> api;
  ParallaxArtworkRecipe recipe{
      .id = "recipe",
      .name = "Cave Wall",
      .source_artwork_id = "source",
      .texture_id = "texture",
  };
  Texture texture{
      .id = "texture", .name = "Cave Wall", .path = "textures/parallax_artwork/texture.png"};
  SourceArtwork source{
      .id = "source",
      .name = "Shared source",
      .source_path = "source_art/source.png",
  };
  tree.Write("definitions/parallax_artwork_recipes/recipe.json");
  WriteTextureFiles(tree, texture);
  WriteSourceFiles(tree, source);

  EXPECT_CALL(api, CheckGeneratedParallaxArtworkDeletable("recipe"))
      .WillOnce(Return(absl::OkStatus()));
  ON_CALL(api, GetParallaxArtworkRecipe("recipe")).WillByDefault(Return(&recipe));
  ON_CALL(api, GetTexture("texture")).WillByDefault(Return(&texture));
  ON_CALL(api, GetSourceArtwork("source")).WillByDefault(Return(&source));
  ON_CALL(api, GetAllPropRecipes())
      .WillByDefault(Return(std::vector<PropRecipe>{
          PropRecipe{.id = "prop", .name = "Crystal", .source_artwork_id = "source"}}));
  ON_CALL(api, GetAllParallaxArtworkRecipes())
      .WillByDefault(Return(std::vector<ParallaxArtworkRecipe>{recipe}));
  EXPECT_CALL(api, DeleteGeneratedParallaxArtwork("recipe"))
      .WillOnce(Return(absl::InternalError("definition is locked")));

  const absl::Status status =
      QuarantineGeneratedAsset(api, Options(tree, GeneratedAssetKind::kParallaxArtwork, "recipe"));

  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
  EXPECT_THAT(std::string(status.message()), HasSubstr("complete recovery snapshot"));
  const nlohmann::json manifest = ReadManifest(tree);
  int retained_files = 0;
  for (const nlohmann::json& file : manifest.at("files")) {
    if (file.at("live_disposition") == "retained_shared_dependency") ++retained_files;
  }
  EXPECT_EQ(retained_files, 2) << manifest.dump(2);
}

TEST(GeneratedAssetQuarantineTest, CapturesTerrainRecipeTilesetAndTexture) {
  TemporaryAssetTree tree;
  NiceMock<MockApi> api;
  TerrainRecipe recipe{
      .id = "recipe",
      .name = "Cave Ground",
      .tileset_id = "tileset",
      .texture_id = "texture",
  };
  Tileset tileset{.id = "tileset", .name = "Cave Ground", .texture_id = "texture"};
  Texture texture{.id = "texture", .name = "Cave Ground", .path = "terrain/texture.png"};
  tree.Write("definitions/terrain_recipes/recipe.json");
  tree.Write("definitions/tilesets/Cave Ground-tileset.json");
  WriteTextureFiles(tree, texture);

  EXPECT_CALL(api, CheckGeneratedTerrainDeletable("recipe")).WillOnce(Return(absl::OkStatus()));
  ON_CALL(api, GetTerrainRecipe("recipe")).WillByDefault(Return(&recipe));
  ON_CALL(api, GetTileset("tileset")).WillByDefault(Return(&tileset));
  ON_CALL(api, GetTexture("texture")).WillByDefault(Return(&texture));
  EXPECT_CALL(api, DeleteGeneratedTerrain("recipe")).WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(QuarantineGeneratedAsset(api, Options(tree, GeneratedAssetKind::kTerrain, "recipe")));
  EXPECT_EQ(ReadManifest(tree).at("files").size(), 4);
}

TEST(GeneratedAssetQuarantineTest, RefusesOutputInsideLiveAssetRoot) {
  TemporaryAssetTree tree;
  NiceMock<MockApi> api;
  GeneratedAssetQuarantineOptions options = Options(tree, GeneratedAssetKind::kProp, "recipe");
  options.output_path = (tree.assets() / "quarantine").string();
  EXPECT_CALL(api, CheckGeneratedPropDeletable(_)).Times(0);

  const absl::Status status = QuarantineGeneratedAsset(api, options);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("outside the live asset root"));
}

}  // namespace
}  // namespace zebes
