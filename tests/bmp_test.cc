#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#pragma pack(push, 1)
struct BitmapFileHeader {
  uint16_t bfType{0x4D42}; // 'BM'
  uint32_t bfSize{0};      // Size of the file (in bytes)
  uint16_t bfReserved1{0}; // Reserved, must be zero
  uint16_t bfReserved2{0}; // Reserved, must be zero
  uint32_t bfOffBits{54};  // Offset to the start of the pixel data
};

struct BitmapInfoHeader {
  uint32_t biSize{40};        // Size of this header (in bytes)
  int32_t biWidth{0};         // Width of the bitmap (in pixels)
  int32_t biHeight{0};        // Height of the bitmap (in pixels)
  uint16_t biPlanes{1};       // Number of planes (must be 1)
  uint16_t biBitCount{24};    // Bits per pixel (24 for RGB)
  uint32_t biCompression{0};  // Compression type (0 for no compression)
  uint32_t biSizeImage{0};    // Image size (in bytes)
  int32_t biXPelsPerMeter{0}; // Horizontal resolution (pixels per meter)
  int32_t biYPelsPerMeter{0}; // Vertical resolution (pixels per meter)
  uint32_t biClrUsed{0};      // Number of colors in the color table
  uint32_t biClrImportant{0}; // Number of important colors
};
#pragma pack(pop)

class Bitmap {
public:
  Bitmap(int width, int height)
      : width_(width), height_(height),
        data_(height, std::vector<uint8_t>(width * 3, 255)) {}

  void set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
      std::cerr << "Index out of bounds" << std::endl;
      return;
    }
    size_t index = (y * width_ + x) * 3;
    data_[y][index + 2] = r;
    data_[y][index + 1] = g;
    data_[y][index] = b;
  }

  bool saveToBMP(const std::string &filename) const {
    BitmapFileHeader fileHeader;
    BitmapInfoHeader infoHeader;

    fileHeader.bfSize = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) +
                        width_ * height_ * 3;
    infoHeader.biWidth = width_;
    infoHeader.biHeight = height_;
    infoHeader.biSizeImage = width_ * height_ * 3;

    std::ofstream outFile(filename, std::ios::out | std::ios::binary);
    if (!outFile) {
      std::cerr << "Could not open the file for writing!" << std::endl;
      return false;
    }

    outFile.write(reinterpret_cast<const char *>(&fileHeader),
                  sizeof(fileHeader));
    outFile.write(reinterpret_cast<const char *>(&infoHeader),
                  sizeof(infoHeader));

    for (int y = height_ - 1; y >= 0; --y) {
      outFile.write(reinterpret_cast<const char *>(data_[y].data()),
                    width_ * 3);
    }

    outFile.close();
    return true;
  }

private:
  int width_, height_;
  std::vector<std::vector<uint8_t>> data_;
};

int main() {
  // Create a 5x5 bitmap
  Bitmap bitmap(5, 5);

  // Set some values in the bitmap
  bitmap.set(0, 0, 255, 0, 0); // Red
  bitmap.set(2, 2, 0, 255, 0); // Green
  bitmap.set(4, 4, 0, 0, 255); // Blue

  // Save the bitmap to a BMP file
  if (bitmap.saveToBMP("/tmp/bitmap.bmp")) {
    std::cout << "Bitmap saved successfully!" << std::endl;
  } else {
    std::cout << "Failed to save bitmap." << std::endl;
  }

  return 0;
}
