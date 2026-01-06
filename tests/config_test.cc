#include "common/config.h"

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "macros.h"

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

TEST(ConfigTest, PathConfig) {
  std::string exec_path = EngineConfig::GetExecPath();
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

TEST(ConfigTest, EngineConfig) {
  ASSERT_OK_AND_ASSIGN(EngineConfig engine_config, EngineConfig::Create());
  nlohmann::json j;
  nlohmann::to_json(j, engine_config);
  LOG(INFO) << j.dump(2);

  int new_fps = 123;
  j["fps"] = new_fps;
  nlohmann::from_json(j, engine_config);
  EXPECT_EQ(engine_config.fps, new_fps);
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
