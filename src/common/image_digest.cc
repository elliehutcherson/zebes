#include "common/image_digest.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>

#include "absl/status/status.h"

namespace zebes {
namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

class Sha256 {
 public:
  void Update(const uint8_t* bytes, size_t size) {
    total_bytes_ += size;
    for (size_t index = 0; index < size; ++index) {
      buffer_[buffer_size_++] = bytes[index];
      if (buffer_size_ == buffer_.size()) {
        Transform();
        buffer_size_ = 0;
      }
    }
  }

  std::array<uint8_t, 32> Final() {
    const uint64_t message_bits = total_bytes_ * 8;
    const size_t padding_size = buffer_size_ < 56 ? 56 - buffer_size_ : 120 - buffer_size_;
    std::array<uint8_t, 64> padding{};
    padding[0] = 0x80;
    Update(padding.data(), padding_size);

    std::array<uint8_t, 8> length{};
    for (size_t index = 0; index < length.size(); ++index) {
      length[length.size() - 1 - index] = static_cast<uint8_t>(message_bits >> (index * 8));
    }
    Update(length.data(), length.size());

    std::array<uint8_t, 32> digest{};
    for (size_t word = 0; word < state_.size(); ++word) {
      for (size_t byte = 0; byte < 4; ++byte) {
        digest[word * 4 + byte] = static_cast<uint8_t>(state_[word] >> ((3 - byte) * 8));
      }
    }
    return digest;
  }

 private:
  void Transform() {
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16; ++index) {
      words[index] = static_cast<uint32_t>(buffer_[index * 4]) << 24 |
                     static_cast<uint32_t>(buffer_[index * 4 + 1]) << 16 |
                     static_cast<uint32_t>(buffer_[index * 4 + 2]) << 8 |
                     static_cast<uint32_t>(buffer_[index * 4 + 3]);
    }
    for (size_t index = 16; index < words.size(); ++index) {
      const uint32_t s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^
                          (words[index - 15] >> 3);
      const uint32_t s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^
                          (words[index - 2] >> 10);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (size_t index = 0; index < words.size(); ++index) {
      const uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const uint32_t choice = (e & f) ^ (~e & g);
      const uint32_t temporary1 = h + sum1 + choice + kRoundConstants[index] + words[index];
      const uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<uint8_t, 64> buffer_{};
  size_t buffer_size_ = 0;
  uint64_t total_bytes_ = 0;
};

}  // namespace

absl::StatusOr<std::string> RgbaImageDigest(const RgbaImage& image) {
  if (!image.IsValid()) return absl::InvalidArgumentError("cannot digest an invalid RGBA image");

  std::array<uint8_t, 8> header{};
  const std::array<uint32_t, 2> dimensions = {static_cast<uint32_t>(image.width),
                                              static_cast<uint32_t>(image.height)};
  for (size_t dimension = 0; dimension < dimensions.size(); ++dimension) {
    for (size_t byte = 0; byte < 4; ++byte) {
      header[dimension * 4 + byte] =
          static_cast<uint8_t>(dimensions[dimension] >> ((3 - byte) * 8));
    }
  }

  Sha256 sha256;
  sha256.Update(header.data(), header.size());
  sha256.Update(image.pixels.data(), image.pixels.size());
  const std::array<uint8_t, 32> digest = sha256.Final();

  std::ostringstream encoded;
  encoded << std::hex << std::setfill('0');
  for (const uint8_t byte : digest) encoded << std::setw(2) << static_cast<int>(byte);
  return encoded.str();
}

}  // namespace zebes
