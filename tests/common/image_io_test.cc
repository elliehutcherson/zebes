#include "common/image_io.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

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

  ASSERT_OK(WritePng(path, 8, 6, pixels));

  const absl::StatusOr<RgbaImage> loaded = ReadPng(path);
  ASSERT_OK(loaded);
  EXPECT_EQ(loaded->width, 8);
  EXPECT_EQ(loaded->height, 6);

  // PNG is lossless, and alpha has to survive: a terrain atlas is mostly
  // transparent and a writer that flattened it would ruin every tile. Derived
  // artwork is identified by hashing exactly these bytes, so a round trip that
  // altered one would make a tile stop matching itself between sessions.
  EXPECT_EQ(loaded->pixels, pixels);
  std::filesystem::remove(path);
}

TEST(ImageIoTest, ReadingSomethingThatIsNotAnImageFails) {
  const std::string path = TempPath("image_io_not_an_image.png");
  std::ofstream(path) << "this is not a PNG";

  EXPECT_FALSE(ReadPng(path).ok());
  std::filesystem::remove(path);
}

TEST(ImageIoTest, ReadingAMissingFileFails) {
  EXPECT_FALSE(ReadPng(TempPath("image_io_absent.png")).ok());
}

TEST(ImageIoTest, CreatesMissingDirectories) {
  const std::string directory = TempPath("image_io_nested");
  std::filesystem::remove_all(directory);
  const std::string path = directory + "/deeper/art.png";

  ASSERT_OK(WritePng(path, 2, 2, Checkerboard(2, 2)));
  EXPECT_TRUE(std::filesystem::exists(path));
  std::filesystem::remove_all(directory);
}

TEST(ImageIoTest, RejectsMismatchedPixelCounts) {
  const std::string path = TempPath("image_io_bad.png");
  EXPECT_FALSE(WritePng(path, 4, 4, Checkerboard(2, 2)).ok());
  EXPECT_FALSE(WritePng(path, 0, 4, {}).ok());
  EXPECT_FALSE(std::filesystem::exists(path));
}

std::vector<uint8_t> EncodedPng(int width, int height) {
  const std::string path = TempPath("image_io_decode_fixture.png");
  std::filesystem::remove(path);
  const absl::Status written = WritePng(path, width, height, Checkerboard(width, height));
  EXPECT_TRUE(written.ok()) << written;
  std::ifstream file(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  return bytes;
}

TEST(ImageIoTest, DecodesEncodedBytesToTheSamePixels) {
  const std::vector<uint8_t> encoded = EncodedPng(8, 6);
  ASSERT_FALSE(encoded.empty());

  ASSERT_OK_AND_ASSIGN(const RgbaImage image, DecodeImage(encoded, 1 << 20));

  EXPECT_EQ(image.width, 8);
  EXPECT_EQ(image.height, 6);
  EXPECT_EQ(image.pixels, Checkerboard(8, 6));
}

TEST(ImageIoTest, EncodesPngBytesWithoutAFile) {
  const RgbaImage original{
      .width = 8,
      .height = 6,
      .pixels = Checkerboard(8, 6),
  };

  ASSERT_OK_AND_ASSIGN(const std::vector<uint8_t> encoded, EncodePng(original));
  ASSERT_OK_AND_ASSIGN(const RgbaImage decoded, DecodeImage(encoded, 1 << 20));

  EXPECT_EQ(decoded.width, original.width);
  EXPECT_EQ(decoded.height, original.height);
  EXPECT_EQ(decoded.pixels, original.pixels);
}

TEST(ImageIoTest, ReportsUndecodableBytesAsDataLoss) {
  const std::string text = "this is not a PNG";
  const std::vector<uint8_t> bytes(text.begin(), text.end());

  EXPECT_EQ(DecodeImage(bytes, 1 << 20).status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(DecodeImage({}, 1 << 20).status().code(), absl::StatusCode::kDataLoss);
}

TEST(ImageIoTest, RefusesAnImageBeyondThePixelLimit) {
  const std::vector<uint8_t> encoded = EncodedPng(8, 6);
  ASSERT_FALSE(encoded.empty());

  const absl::Status status = DecodeImage(encoded, 47).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_TRUE(DecodeImage(encoded, 48).ok()) << "48 pixels is exactly an 8x6 image";
}

TEST(ImageIoTest, RejectsAnInvalidPixelLimit) {
  const std::vector<uint8_t> encoded = EncodedPng(2, 2);

  EXPECT_EQ(DecodeImage(encoded, 0).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(DecodeImage(encoded, -1).status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
