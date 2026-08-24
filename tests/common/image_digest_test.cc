#include "common/image_digest.h"

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(ImageDigestTest, UsesCanonicalDimensionsAndDecodedRgbaBytes) {
  const RgbaImage image{.width = 1, .height = 1, .pixels = {1, 2, 3, 4}};

  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(image));

  EXPECT_EQ(digest, "658c79cf1745b734bd985472a1fa79f55e975f7fedd2fd717cf32b618542dccf");
}

TEST(ImageDigestTest, DimensionsArePartOfTheIdentity) {
  const RgbaImage horizontal{.width = 2, .height = 1, .pixels = {1, 2, 3, 4, 5, 6, 7, 8}};
  const RgbaImage vertical{.width = 1, .height = 2, .pixels = {1, 2, 3, 4, 5, 6, 7, 8}};

  ASSERT_OK_AND_ASSIGN(const std::string horizontal_digest, RgbaImageDigest(horizontal));
  ASSERT_OK_AND_ASSIGN(const std::string vertical_digest, RgbaImageDigest(vertical));
  EXPECT_NE(horizontal_digest, vertical_digest);
}

TEST(ImageDigestTest, RejectsInvalidStorage) {
  const RgbaImage invalid{.width = 1, .height = 1, .pixels = {1, 2, 3}};
  EXPECT_FALSE(RgbaImageDigest(invalid).ok());
}

TEST(ImageDigestTest, RecognizesOnlyCanonicalLowercaseSha256Text) {
  EXPECT_TRUE(IsLowercaseSha256Digest(std::string(64, '0')));
  EXPECT_TRUE(IsLowercaseSha256Digest(std::string(64, 'f')));
  EXPECT_FALSE(IsLowercaseSha256Digest(std::string(63, '0')));
  EXPECT_FALSE(IsLowercaseSha256Digest(std::string(64, 'F')));
  EXPECT_FALSE(IsLowercaseSha256Digest(std::string(64, 'g')));
}

}  // namespace
}  // namespace zebes
