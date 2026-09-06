#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "artwork/layered_puppet.h"
#include "artwork/layered_puppet_editor_state.h"
#include "artwork/skeleton_rig.h"

namespace zebes {

// Produces a self-contained browser editor for a validated layered puppet.
// The generated document loads source.png and parts/<name>.png beside itself.
// It supports non-deforming source-joint calibration, target-pose deformation,
// freeform full-canvas mesh brushing, scrollable zoom, persistent state, and
// PNG/JSON export. Source pixels, seed meshes, bone chains, and draw order
// originate in the C++ puppet pipeline; the browser owns only interactive mesh
// paint and affine preview state.
// Builds the immutable contract used to reject saved state from another source
// image, puppet topology, or frame set.
absl::StatusOr<LayeredPuppetEditorStateContract> BuildLayeredPuppetEditorStateContract(
    const LayeredPuppet& puppet, std::string_view source_rgba_digest,
    const SkeletonRig* reference_rig, const SkeletonRigClip* reference_clip);

// Embeds one validated puppet and matching state contract in the editor page.
absl::StatusOr<std::string> RenderLayeredPuppetEditorHtml(
    const LayeredPuppet& puppet, size_t initial_pose_index,
    const LayeredPuppetEditorStateContract& contract, const SkeletonRig* reference_rig,
    const SkeletonRigClip* reference_clip);

}  // namespace zebes
