#pragma once

#include "common/image_io.h"

namespace zebes {

// An image plus the pixel inside it that maps to an entity's world position.
// Stages preserve this value explicitly so transparent canvas padding cannot
// silently move a grounded prop.
struct PropArtwork {
  RgbaImage image;
  int anchor_x = 0;
  int anchor_y = 0;

  bool IsValid() const {
    return image.IsValid() && anchor_x >= 0 && anchor_y >= 0 && anchor_x < image.width &&
           anchor_y < image.height;
  }
};

}  // namespace zebes
