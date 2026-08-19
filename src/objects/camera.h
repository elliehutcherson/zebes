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

// A 2D view onto the world. Plain state: whoever owns a Camera decides how it
// moves and what limits it obeys.
//
// position is the centre of the view; screen space has its origin at the
// top-left, which is the half-viewport term in the transforms below. World y
// increases downward as screen y does, so neither transform flips an axis.
//
// The viewport must be set before either transform means anything, and zoom
// must stay positive since both divide by it. Nothing here enforces that: the
// valid interval belongs to the owner. See CameraZoomRange.
struct Camera {
  Vec position;

  // Pixels per world unit.
  double zoom = 1.0;

  int viewport_width = 0;
  int viewport_height = 0;

  Vec WorldToScreen(const Vec& world_pos) const {
    Vec screen_pos;
    double rel_x = world_pos.x - position.x;
    double rel_y = world_pos.y - position.y;

    rel_x *= zoom;
    rel_y *= zoom;

    screen_pos.x = rel_x + (viewport_width / 2.0);
    screen_pos.y = rel_y + (viewport_height / 2.0);

    return screen_pos;
  }

  Vec ScreenToWorld(const Vec& screen_pos) const {
    Vec world_pos;

    double rel_x = screen_pos.x - (viewport_width / 2.0);
    double rel_y = screen_pos.y - (viewport_height / 2.0);

    rel_x /= zoom;
    rel_y /= zoom;

    world_pos.x = rel_x + position.x;
    world_pos.y = rel_y + position.y;

    return world_pos;
  }

  // Where to put a parallax layer's top-left pixel in world space this frame.
  // The caller draws the image there, so a layer needs no scroll state of its
  // own.
  //
  // scroll_factor 1 pins the image to the world so it scrolls normally; 0 pins
  // it to the camera so it never appears to move, which is how a distant
  // backdrop is spelled. `offset` is the world point where layers line up
  // whatever their factor.
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
