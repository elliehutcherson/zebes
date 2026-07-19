#pragma once

namespace zebes {

// Logical dimensions of a game camera in world units.
struct GameViewSize {
  int width = 640;
  int height = 360;

  constexpr bool IsValid() const { return width > 0 && height > 0; }
};

}  // namespace zebes
