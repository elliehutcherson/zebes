#include "platform/headless/headless_texture_store.h"

#include <filesystem>
#include <string>

#include "common/image_io.h"
#include "common/utils.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

class TemporaryPng {
 public:
  TemporaryPng()
      : path_(std::filesystem::temp_directory_path() /
              std::filesystem::path("zebes-headless-texture-" + GenerateGuid() + ".png")) {}
  ~TemporaryPng() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  std::string path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

TEST(HeadlessTextureStoreTest, DecodesFilesAndEnforcesHandleOwnership) {
  TemporaryPng png;
  const std::vector<uint8_t> pixels(2 * 2 * 4, 255);
  ASSERT_OK(WritePng(png.path(), 2, 2, pixels));

  HeadlessTextureStore first;
  HeadlessTextureStore second;
  ASSERT_OK_AND_ASSIGN(TextureHandle handle, first.Load(png.path()));
  EXPECT_FALSE(second.Unload(handle).ok());
  EXPECT_OK(first.Unload(handle));
  EXPECT_TRUE(absl::IsNotFound(first.Unload(handle)));
}

TEST(HeadlessTextureStoreTest, RejectsMissingOrInvalidPixelResources) {
  HeadlessTextureStore store;
  EXPECT_FALSE(store.Load("/definitely/missing/zebes.png").ok());
  EXPECT_FALSE(store.LoadFromPixels(2, 2, std::vector<uint8_t>(3)).ok());
}

}  // namespace
}  // namespace zebes
