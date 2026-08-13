#include "common/image_io.h"

#include <filesystem>
#include <vector>

#include "gtest/gtest.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace zebes {
namespace {

std::string TempPath(const std::string& name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

std::vector<uint8_t> Checkerboard(int width, int height) {
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t index = (static_cast<size_t>(y) * width + x) * 4;
      const bool light = (x + y) % 2 == 0;
      pixels[index + 0] = light ? 200 : 20;
      pixels[index + 1] = static_cast<uint8_t>(x * 8);
      pixels[index + 2] = static_cast<uint8_t>(y * 8);
      pixels[index + 3] = light ? 255 : 128;
    }
  }
  return pixels;
}

TEST(ImageIoTest, WrittenPixelsReadBackExactly) {
  const std::string path = TempPath("image_io_roundtrip.png");
  std::filesystem::remove(path);
  const std::vector<uint8_t> pixels = Checkerboard(8, 6);

  ASSERT_TRUE(WritePng(path, 8, 6, pixels).ok());

  int width = 0;
  int height = 0;
  int channels = 0;
  uint8_t* loaded = stbi_load(path.c_str(), &width, &height, &channels, 4);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(width, 8);
  EXPECT_EQ(height, 6);

  // PNG is lossless, and alpha has to survive: a terrain atlas is mostly
  // transparent and a writer that flattened it would ruin every tile.
  for (size_t i = 0; i < pixels.size(); ++i) {
    EXPECT_EQ(loaded[i], pixels[i]) << "byte " << i << " changed on the round trip";
  }
  stbi_image_free(loaded);
  std::filesystem::remove(path);
}

TEST(ImageIoTest, CreatesMissingDirectories) {
  const std::string directory = TempPath("image_io_nested");
  std::filesystem::remove_all(directory);
  const std::string path = directory + "/deeper/art.png";

  ASSERT_TRUE(WritePng(path, 2, 2, Checkerboard(2, 2)).ok());
  EXPECT_TRUE(std::filesystem::exists(path));
  std::filesystem::remove_all(directory);
}

TEST(ImageIoTest, RejectsMismatchedPixelCounts) {
  const std::string path = TempPath("image_io_bad.png");
  EXPECT_FALSE(WritePng(path, 4, 4, Checkerboard(2, 2)).ok());
  EXPECT_FALSE(WritePng(path, 0, 4, {}).ok());
  EXPECT_FALSE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace zebes
