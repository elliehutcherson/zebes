#pragma once

#include <algorithm>

#include "objects/vec.h"

namespace zebes {

// Valid zoom interval supplied by the system that controls a camera. Camera
// state itself does not decide whether it belongs to gameplay or an editor.
struct CameraZoomRange {
  double minimum;
  double maximum;

  constexpr bool IsValid() const { return minimum > 0.0 && maximum >= minimum; }
  constexpr double Clamp(double zoom) const { return std::clamp(zoom, minimum, maximum); }
};

struct Camera {
  // Center of the camera view in World Coordinates
  Vec position;

  // Zoom level (1.0 = normal, 2.0 = zoomed in, 0.5 = zoomed out)
  double zoom = 1.0;

  // The dimensions of the viewport on screen (e.g., 800x600)
  int viewport_width = 0;
  int viewport_height = 0;

  // Convert World Coordinate -> Screen Pixel Coordinate
  // Used for Rendering: "Where do I draw this sprite?"
  Vec WorldToScreen(const Vec& world_pos) const {
    Vec screen_pos;
    // 1. Translate world relative to camera
    double rel_x = world_pos.x - position.x;
    double rel_y = world_pos.y - position.y;

    // 2. Apply Zoom
    rel_x *= zoom;
    rel_y *= zoom;

    // 3. Offset to center of screen (since 0,0 is top-left in SDL)
    screen_pos.x = rel_x + (viewport_width / 2.0);
    screen_pos.y = rel_y + (viewport_height / 2.0);

    return screen_pos;
  }

  // Convert Screen Pixel Coordinate -> World Coordinate
  // Used for Picking: "What did the mouse click on?"
  Vec ScreenToWorld(const Vec& screen_pos) const {
    Vec world_pos;

    // 1. Remove screen center offset
    double rel_x = screen_pos.x - (viewport_width / 2.0);
    double rel_y = screen_pos.y - (viewport_height / 2.0);

    // 2. Reverse Zoom
    rel_x /= zoom;
    rel_y /= zoom;

    // 3. Add camera position
    world_pos.x = rel_x + position.x;
    world_pos.y = rel_y + position.y;

    return world_pos;
  }

  // Calculate the parallax view offset into the image.
  // Returns the top-left corner of what the parallax layer should show.
  // Vec ParallaxOffset(Vec scroll_factor, Vec offset = {0, 0}) const {
  //   // The anchor is where parallax and world perfectly align
  //   double anchor_x = offset.x + viewport_width / 2.0;
  //   double anchor_y = offset.y + viewport_height / 2.0;

  //   // How far the camera has moved from the anchor
  //   double delta_x = position.x - anchor_x;
  //   double delta_y = position.y - anchor_y;

  //   // Parallax view shifts at scroll_factor rate
  //   Vec result;
  //   result.x = offset.x + delta_x * scroll_factor.x;
  //   result.y = offset.y + delta_y * scroll_factor.y;
  //   return result;
  // }

  // Returns where image pixel (0,0) should be placed in world space
  // for correct parallax rendering.
  Vec ParallaxWorldOrigin(Vec scroll_factor, Vec offset = {0, 0}) const {
    double world_left = position.x - viewport_width / (2.0 * zoom);
    double world_top = position.y - viewport_height / (2.0 * zoom);
    Vec result;
    result.x = offset.x + (world_left - offset.x) * (1.0 - scroll_factor.x);
    result.y = offset.y + (world_top - offset.y) * (1.0 - scroll_factor.y);
    return result;
  }
};

}  // namespace zebes
