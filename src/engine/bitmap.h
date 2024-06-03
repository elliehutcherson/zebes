#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"

#pragma pack(push, 1)
struct BitmapFileHeader {
  uint16_t bf_type{0x4D42};    // 'BM'
  uint32_t bf_size{0};         // Size of the file (in bytes)
  uint16_t bf_reserved1{0};    // Reserved, must be zero
  uint16_t bf_reserved2{0};    // Reserved, must be zero
  uint32_t bf_offset_bits{54}; // Offset to the start of the pixel data
};

struct BitmapInfoHeader {
  uint32_t bi_size{40};             // Size of this header (in bytes)
  int32_t bi_width{0};              // Width of the bitmap (in pixels)
  int32_t bi_height{0};             // Height of the bitmap (in pixels)
  uint16_t bi_planes{1};            // Number of planes (must be 1)
  uint16_t bi_bit_count{24};        // Bits per pixel (24 for RGB)
  uint32_t bi_compression{0};       // Compression type (0 for no compression)
  uint32_t bi_size_image{0};        // Image size (in bytes)
  int32_t bi_x_pixels_per_meter{0}; // Horizontal resolution (pixels per meter)
  int32_t bi_y_pixels_per_meter{0}; // Vertical resolution (pixels per meter)
  uint32_t bi_colors_used{0};       // Number of colors in the color table
  uint32_t bi_colors_important{0};  // Number of important colors
};
#pragma pack(pop)

class Bitmap {
public:
  Bitmap(int width, int height)
      : width_(width), height_(height),
        data_(height, std::vector<uint8_t>(width * 3, 255)) {}

  absl::Status Set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
      return absl::InvalidArgumentError("Index out of bounds");
    } else {
      LOG(INFO) << absl::StrFormat("Setting pixel at (%d, %d) to (%u,%u,%u)", x,
                                   y, r, g, b);
    }
    size_t index = (y * width_ + x) * 3;
    data_[y][index + 2] = r;
    data_[y][index + 1] = g;
    data_[y][index] = b;

    return absl::OkStatus();
  }

  absl::Status SaveToBmp(const std::string &filename) const {
    BitmapFileHeader file_header;
    BitmapInfoHeader info_header;

    file_header.bf_size = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) +
                          width_ * height_ * 3;
    info_header.bi_width = width_;
    info_header.bi_height = height_;
    info_header.bi_size_image = width_ * height_ * 3;

    std::ofstream out_file(filename, std::ios::out | std::ios::binary);
    if (!out_file) {
      return absl::AbortedError("Could not open the file for writing!");
    }

    out_file.write(reinterpret_cast<const char *>(&file_header),
                   sizeof(file_header));
    out_file.write(reinterpret_cast<const char *>(&info_header),
                   sizeof(info_header));

    for (int y = height_ - 1; y >= 0; --y) {
      out_file.write(reinterpret_cast<const char *>(data_[y].data()),
                     width_ * 3);
    }

    out_file.close();
    return absl::OkStatus();
  }

private:
  int width_, height_;
  std::vector<std::vector<uint8_t>> data_;
};
