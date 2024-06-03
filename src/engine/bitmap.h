#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

inline constexpr uint16_t kBmpType = 0x4D42;

#pragma pack(push, 1)
struct BitmapFileHeader {
  uint16_t bf_type{kBmpType};  // 'BM'
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
  static absl::StatusOr<Bitmap> LoadFromBmp(const std::string &filename) {
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (!file) {
      return absl::AbortedError("Could not open the file for reading!");
    }

    BitmapFileHeader file_header;
    BitmapInfoHeader info_header;

    file.read(reinterpret_cast<char *>(&file_header), sizeof(file_header));
    file.read(reinterpret_cast<char *>(&info_header), sizeof(info_header));

    if (file_header.bf_type != kBmpType) { // Check if it's a BMP file
      return absl::AbortedError("Not a valid BMP file!");
    }

    Bitmap bitmap(info_header.bi_width, info_header.bi_height);

    for (int y = info_header.bi_height - 1; y >= 0; --y) {
      file.read(reinterpret_cast<char *>(bitmap.data_[y].data()), info_header.bi_width * 3);
    }

    file.close();
    return bitmap; 
  }

  Bitmap(int width, int height)
      : width_(width), height_(height),
        data_(height, std::vector<uint8_t>(width * 3, 255)) {}

  absl::Status Set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
      return absl::InvalidArgumentError("Index out of bounds");
    }
    size_t index = (y * width_ + x) * 3;
    data_[y][index + 2] = r;
    data_[y][index + 1] = g;
    data_[y][index] = b;

    return absl::OkStatus();
  }

  void Resize(int width, int height) {
    data_.clear();
    width_ = width;
    height_ = height;
    data_.resize(height_, std::vector<uint8_t>(width_ * 3, 255));
  }

  void Clear() {
    data_.clear();
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

  std::string ToString(size_t max_rows = 5, size_t max_cols = 5) const {
    std::ostringstream oss;
    max_rows = std::min(max_rows, static_cast<size_t>(height_));
    max_cols = std::min(max_cols, static_cast<size_t>(width_));
    oss << "Bitmap width: " << width_ << ", height: " << height_ << std::endl;

    for (size_t row = 0; row < max_rows; ++row) {
      for (size_t col = 0; col < max_cols; ++col) {
        size_t index = col * 3;
        oss << "(" << static_cast<int>(data_[row][index]) << ", "
            << static_cast<int>(data_[row][index + 1]) << ", "
            << static_cast<int>(data_[row][index + 2]) << ") ";
      }
      if (max_cols < static_cast<size_t>(width_)) {
        oss << "...";
      }
      oss << std::endl;
    }
    if (max_rows < static_cast<size_t>(height_)) {
      oss << "..." << std::endl;
    }
    return oss.str();
  }

  void Print() const { LOG(INFO) << ToString(); }

private:
  int width_ = 0;
  int height_ = 0;
  std::vector<std::vector<uint8_t>> data_;
};
