#pragma once 

#include <string>
#include <vector>

#include "absl/status/status.h"

#include "config.h"

namespace zebes {

class TileMatrix {
  public:
    TileMatrix(const GameConfig* config);
    ~TileMatrix() = default;
    // Initialize the tile matrix for the values stored at the given path. 
    absl::Status Init();
    // Initialize the tile matrix for the values stored at the given path. 
    absl::Status InitFromValues(std::string values);
    // Insert a tile type for the given coordinates.
    void Insert(int x, int y, int value);
    // Get the tile type for the given coordinates.
    int Get(int x, int y) const;
    // Get the size of the tile matrix in the x direction. 
    int x_max() const { return x_size_; }
    // Get the size of the tile matrix in the y direction. 
    int y_max() const { return y_size_; }

  private:
    const GameConfig* config_;
    const int x_size_;
    const int y_size_;
    std::vector<float> matrix_;
};

}  // namespace zebes