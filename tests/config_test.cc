#include "engine/config.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace zebes {
namespace {

TEST(ConfigTest, WindowConfig) {
  WindowConfig window_config;
  nlohmann::json j;
  nlohmann::to_json(j, window_config);
  LOG(INFO) << j.dump(2);

  std::string new_title = "new tile";
  j["title"] = new_title;
  nlohmann::from_json(j, window_config);
  EXPECT_EQ(window_config.title, new_title);
}

TEST(ConfigTest, BoundaryConfig) {
  BoundaryConfig boundary_config;
  nlohmann::json j;
  nlohmann::to_json(j, boundary_config);
  LOG(INFO) << j.dump(2);

  int new_x_max = 1234;
  j["x_max"] = new_x_max;
  nlohmann::from_json(j, boundary_config);
  EXPECT_EQ(boundary_config.x_max , new_x_max);
}

TEST(ConfigTest, TileConfig) {
  TileConfig tile_config;
  nlohmann::json j;
  nlohmann::to_json(j, tile_config);
  LOG(INFO) << j.dump(2);

  int new_scale = 3;
  j["scale"] = new_scale;
  nlohmann::from_json(j, tile_config);
  EXPECT_EQ(tile_config.scale, new_scale);
}

TEST(ConfigTest, CollisionConfig) {
  CollisionConfig collision_config;
  nlohmann::json j;
  nlohmann::to_json(j, collision_config);
  LOG(INFO) << j.dump(2);

  float new_area_width = 1234.5;
  j["area_width"] = new_area_width;
  nlohmann::from_json(j, collision_config);
  EXPECT_EQ(collision_config.area_width, new_area_width);
}

TEST(ConfigTest, PathConfig) {
  std::string exec_path = GameConfig::GetExecPath();
  LOG(INFO) << "exec_path: " << exec_path;

  PathConfig path_config(exec_path);
  nlohmann::json j;
  nlohmann::to_json(j, path_config);
  LOG(INFO) << j.dump(2);

  std::string new_relative_assets = "new assets";
  j["relative_assets"] = new_relative_assets;
  nlohmann::from_json(j, path_config);
  EXPECT_EQ(path_config.relative_assets, new_relative_assets);

  EXPECT_TRUE(std::filesystem::exists(path_config.execute()));
}

TEST(ConfigTest, GameConfig) {
  GameConfig game_config = GameConfig::Create();
  nlohmann::json j;
  nlohmann::to_json(j, game_config);
  LOG(INFO) << j.dump(2);

  int new_fps = 123;
  j["fps"] = new_fps;
  nlohmann::from_json(j, game_config);
  EXPECT_EQ(game_config.fps, new_fps);
}

} // namespace
} // namespace zebes


int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
