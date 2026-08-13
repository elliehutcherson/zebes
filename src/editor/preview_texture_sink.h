#pragma once

#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "terrain/blob47_compose.h"

namespace zebes {

// Uploads artwork held in memory to the GPU so a panel can draw it.
//
// A live preview redraws its image whenever a control moves, so it never has a
// file behind it and cannot go through the ordinary texture-loading path. The
// SDL implementation lives with the editor's other renderer-facing code; tests
// substitute a stub, which is what keeps the panels testable without a window.
class PreviewTextureSink {
 public:
  virtual ~PreviewTextureSink() = default;

  // Returns a handle valid until the next call. Implementations reuse one
  // texture rather than allocating per frame.
  virtual absl::StatusOr<ImTextureID> Upload(const RgbaImage& image) = 0;
};

}  // namespace zebes
