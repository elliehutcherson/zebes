#include "gtest/gtest.h"

#include "macros.h"
#include "engine/config.h"
#include "engine/creator_api.h"

namespace zebes {
namespace {

TEST(CreatorApiTest, CreateSucceeds) {
  std::string import_path = GameConfig::GetExecPath();
  CreatorApi::Options options = {import_path};
  ASSERT_OK_AND_ASSIGN(CreatorApi creator_api, CreatorApi::Create(options));
}

} // namespace
} // namespace zebes

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}