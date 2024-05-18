#include "tile_matrix.h"

#include <fstream>
#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/status/status.h"

#include "config.h"

namespace zebes {

TileMatrix::TileMatrix(const GameConfig* config) : config_(config), 
x_size_(config_->tiles.size_x), y_size_(config_->tiles.size_x), 
matrix_(std::vector<float>(x_size_ * y_size_, kNoTile)) {};

absl::Status TileMatrix::Init() {
  std::ifstream input(config_->paths.tile_matrix);
  int y = 0;
  for(std::string line; getline(input, line);) {
    int x  = 0;
    std::vector<std::string> line_values = absl::StrSplit(line, ",");
    for (const std::string& line_value : line_values) {
      Insert(x, y, std::stoi(line_value));
      ++x;
    }
    ++y;
  }
  return absl::OkStatus();
}

absl::Status TileMatrix::InitFromValues(std::string values) {
  int y = 0;
  for(const std::string_view line : absl::StrSplit(values, "\n")) {
    int x  = 0;
    std::vector<std::string> line_values = absl::StrSplit(line, ",");
    for (const std::string& line_value : line_values) {
      Insert(x, y, std::stoi(line_value));
      ++x;
    }
    ++y;
  }
  return absl::OkStatus();
}

void TileMatrix::Insert(int x, int y, int value) {
  matrix_[x + (x_size_ * y)] = value; 
}

int TileMatrix::Get(int x, int y) const {
  return matrix_[x + (x_size_ * y)]; 
}

}  // namespace zebes