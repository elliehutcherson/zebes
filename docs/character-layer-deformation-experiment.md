# Character layer-deformation experiment

**Status:** proposed ARAP comparison; ready for review.

## Decision summary

The layered 2D direction remains the best match for the approved character
style. See-through can provide useful hidden-surface candidates, and the C++
pipeline can now preserve visible pixels, enforce exclusive ownership, and
composite underpaint without a ghost arm.

The current mesh deformation is not sufficient. It is a regular texture grid
whose vertices use two rigid bone transforms with a narrow linear blend around
the elbow. Most sleeve pixels therefore behave like one of two rigid strips;
only the blend band morphs, and strong bends compress roughly 13% of the arm's
opaque area. The mesh is transporting texture correctly, but it is not solving
for a coherent surrounding shape.

**Next experiment:** retain the same accepted arm artwork, ownership masks, and
three-layer compositor, but compare the current linear skinning baseline against
a C++ as-rigid-as-possible (ARAP) deformation solve. Do not proceed to the
second arm or legs until this visual-deformation gate is resolved.

## Goal

Prove that the shoulder, elbow, and wrist can act as deformation constraints
whose motion propagates through the complete arm artwork while preserving its
local width, outline, pixel clusters, and attachment to the body at native
48px.

A passing result establishes this production boundary:

```text
approved or accepted-completion RGBA part
        |
        v
exclusive visible ownership + hidden underpaint
        |
        v
explicit three-joint skeleton constraints
        |
        v
ARAP-deformed 2D texture mesh
        |
        v
deterministic 48px frame sheets
        |
        v
existing animation import and runtime pipeline
```

This experiment is not a full animation cycle. It answers whether a generic
shape-preserving solver can produce useful limb articulation from one completed
2D layer.

## Context and findings

The complete measured history remains in
[`experiments/character_binding/FINDINGS.md`](../experiments/character_binding/FINDINGS.md).
The decisions that lead directly to this experiment are:

1. Independently generated animation frames retained pose but drifted in body
   mass, face, and costume.
2. Canny, ordinal depth, and combined controls could not preserve identity while
   enforcing elevation and limb order. Diffusion-based animation control is
   closed.
3. Direct deformation preserved the approved reference but could not invent
   limbs hidden by the long coat.
4. Primitive 3D preserved structure but failed the desired pixel-art style.
5. Explicit layered 2D artwork preserved the approved style, but rigid
   upper/lower pieces exposed seams.
6. See-through V3 produced useful complete arm, boot, and coat candidates. It
   failed legs and tail and hallucinated human ears and hair, so it is accepted
   only as an offline candidate generator.
7. One See-through arm was imported in C++, reduced to the 256px working canvas,
   and bound to shoulder/elbow/wrist through a 286-vertex, 504-triangle grid.
8. Isolated motion remained connected and neutral reconstruction was exact, but
   full-character review exposed a static ghost arm. The torso still owned the
   original arm pixels.
9. Exclusive ownership and a three-layer underpaint/arm/visible-body stack fixed
   the ghost. All 18,974 source pixels are now singly owned; none are unowned,
   multiply owned, or owned outside the source. The full neutral composite has
   zero changed pixels.
10. Human review then identified the remaining defect: the mesh still reads as
    rigid sections rather than visibly morphing the pixels around the skeleton.

### Current linear-skinning evidence

| Pose | Connected components | Opaque pixels | Retained area |
|---|---:|---:|---:|
| neutral | 1 | 1,975 | 100.0% |
| contact | 1 | 1,721 | 87.1% |
| passing | 1 | 1,950 | 98.7% |
| airborne | 1 | 1,717 | 86.9% |

These measurements prove deterministic transport and connectivity. They do not
prove useful deformation. The area loss and rigid visual read make the current
linear blend a rejected production result.

## Current implementation boundary

Relevant C++ code:

- `src/artwork/semantic_layer_import.{h,cc}` restores model crops, performs
  exact-factor reduction, clips inferred alpha, preserves source-visible pixels,
  and measures ownership.
- `src/artwork/layered_puppet.{h,cc}` owns part meshes, bone chains, current
  linear weights, triangle texture rasterization, composition, and native-frame
  reduction.
- `scripts/render_layered_puppet.cc` parses the explicit spec, imports accepted
  See-through layers, publishes isolated/full-pose evidence, and enforces hard
  gates.
- `experiments/character_binding/puppet_specs/mouse_semantic_arm_v1.json` owns
  the selected semantic arm, skeleton, poses, visible mask, underpaint order,
  mesh spacing, and blend radius.

The current compositor is valid and remains unchanged for the ARAP comparison:

1. `body_underpaint`: accepted `topwear` completion, clipped to the approved
   source alpha;
2. `front_arm`: completed semantic arm plus exact original visible arm pixels;
3. `body_visible`: exact source pixels with the arm ownership region removed.

Pose-specific drawing order permits the complete arm to move behind or in front
of the visible body without revealing a static duplicate.

## Why the current mesh is insufficient

The current grid is a rasterization substrate with a local weight field:

- vertices before the elbow blend follow the upper-arm transform;
- vertices after it follow the forearm transform;
- only vertices within six working pixels of the elbow interpolate;
- pixels inherit vertex motion through barycentric texture sampling.

This keeps most source pixels rigid and compresses the narrow transition when
bone rotations diverge. It does not minimize shape distortion across neighboring
vertices, preserve local edge lengths, or redistribute bending across the sleeve.
The intended deformation-cage behavior requires a solver whose constraints
propagate through the mesh.

## Hypothesis

Given the same rest mesh and shoulder/elbow/wrist targets, a 2D ARAP solve will
preserve local geometry while distributing the bend through the sleeve. It
should retain more projected area, maintain a smoother outline, and create a
visibly coherent elbow at 48px without changing neutral pixels or ownership.

## Non-goals

- No new image generation or model training.
- No skeleton conditioning added to See-through.
- No second arm, leg, tail, run cycle, or production import.
- No changes to the stable Blueprint, six state keys, timings, playback, or
  32x64 collider.
- No Python runtime or numerical dependency in the engine.
- No ARAP integration into real-time gameplay; this remains an offline frame
  authoring operation.

## Implementation plan

### Phase 1: freeze the A/B inputs

Use exactly the current accepted artifacts:

- the approved 256px source image and its recorded digest;
- `handwear-l` restored from the recorded See-through crop;
- the tightened authoritative visible-arm mask;
- the accepted `topwear` underpaint;
- the existing skeleton and four target poses;
- the current three-layer draw order.

Retain the current linear-skinning output as the baseline. The ARAP route must
not receive different artwork, masks, poses, framing, or downsampling.

### Phase 2: build an active deformation mesh

Replace the full rectangular grid with an active mesh derived from the arm alpha:

1. Start from the existing regular working-resolution grid.
2. Retain cells that intersect the arm alpha plus one cell of transparent collar
   around the contour.
3. Remove unreferenced vertices.
4. Preserve source UV positions for deterministic texture lookup.
5. Build vertex-to-vertex adjacency and triangle rest data once.
6. Record boundary vertices, rest signed areas, and rest edge lengths for later
   diagnostics.

The collar ensures every approved arm pixel remains covered in neutral while
preventing distant transparent grid regions from influencing the solve.

### Phase 3: define skeleton constraints

The source and target joint chains are:

```text
shoulder -> elbow -> wrist
```

Constraints:

- Pin the vertex nearest each joint to the exact target joint.
- Pin a small shoulder attachment handle to the upper-arm rigid transform so the
  sleeve remains seated under the coat.
- Pin a small wrist handle to the forearm rigid transform so the hand orientation
  follows the lower bone.
- Keep the elbow center pinned, but leave surrounding sleeve vertices free to
  distribute the bend.
- Fail if a required joint has no mesh vertex within the declared handle radius.

Constraint selection is computed once from the source mesh and reused for every
pose. It must not depend on whichever output looks best.

### Phase 4: implement deterministic 2D ARAP in C++

Add a focused artwork library rather than embedding numerical code in the CLI.
The expected API boundary is:

```cpp
struct ArapMesh;
struct ArapConstraints;
struct ArapConfig;
struct ArapResult;

absl::StatusOr<ArapResult> DeformArap(
    const ArapMesh& rest,
    const ArapConstraints& constraints,
    const ArapConfig& config);
```

Implementation:

1. Precompute cotangent edge weights and the constrained Laplacian from the rest
   mesh.
2. **Local step:** estimate one 2x2 rotation per vertex from current and rest
   edge covariance. Use a closed-form 2D polar rotation; no general SVD library
   is required.
3. **Global step:** solve constrained X and Y Laplacian systems.
4. Use a deterministic sparse conjugate-gradient solver with fixed ordering,
   bounded iterations, and an explicit residual tolerance.
5. Repeat local/global steps until the fixed convergence criterion or iteration
   cap.
6. Return convergence statistics, residuals, target-joint errors, and deformed
   vertices.

Solver settings must be fixed by synthetic convergence tests before reviewing
mouse art. Do not sweep them against the desired picture.

### Phase 5: retain the existing texture rasterizer

Use the solved target vertices with the existing inverse triangle rasterization:

- barycentrically map target pixels to source UV positions;
- nearest-sample the accepted arm RGBA;
- preserve binary alpha and the existing native reduction;
- composite with the unchanged underpaint and visible-body layers.

ARAP changes vertex positions only. It does not repaint, regenerate, or alter
pixel ownership.

### Phase 6: add solver and deformation tests

Platform-neutral tests must cover:

1. Identity constraints return the exact rest vertices.
2. A rectangular strip bent 90 degrees preserves connectivity and local edge
   lengths.
3. Repeated solves return identical vertices, statistics, and rendered digests.
4. Missing handles, zero-area triangles, disconnected meshes, non-finite input,
   non-convergence, and invalid solver settings fail explicitly.
5. The mouse neutral part and full composite remain pixel-exact.
6. No triangle changes orientation.
7. Every shoulder/elbow/wrist target is reached within the allowed tolerance.
8. The ownership and ghost-arm regression gates remain unchanged.

### Phase 7: publish direct A/B evidence

For each of neutral, contact, passing, and airborne, publish:

- current linear-skinning part pose;
- ARAP part pose;
- native 48px full character;
- enlarged nearest-neighbor view;
- mesh and constraint overlay;
- alpha and outline difference;
- retained-area and edge-distortion metrics;
- triangle inversion count;
- joint target error;
- solver iterations and residual;
- deterministic RGBA digest.

Publish both transparent-background and focused Catacombs evidence. Human review
must compare the two methods at native 48px before viewing enlargements.

## Acceptance criteria

The ARAP experiment passes only if all hard invariants hold:

- 18,974 source-visible pixels remain singly owned.
- Zero unowned, multiply-owned, or outside-source ownership pixels.
- Full neutral composite changes zero RGBA pixels.
- Every arm pose remains one connected component.
- Zero inverted or zero-area output triangles.
- Shoulder, elbow, and wrist meet their targets within one working-resolution
  pixel.
- Repeated runs produce identical manifests and image digests.
- No transparent crack appears at the shoulder or elbow.
- No static or duplicated arm appears in the complete character.

It must also improve the deformation result:

- contact and airborne retain at least 95% of neutral arm area, compared with
  the current 87.1% and 86.9%; or a documented silhouette change demonstrates
  why projected area is not the correct measure;
- sleeve width and outline flow continuously around the elbow;
- the result reads as one bending painted arm, not two rigid pieces;
- native 48px human review prefers ARAP over the linear baseline;
- the character remains clear against Catacombs at 1x and 2x.

The human native-size comparison is a required gate. Connectivity and numerical
success alone cannot accept the result again.

## Stop rules

- If ARAP violates ownership, neutral identity, determinism, connectivity, or
  triangle orientation, reject the implementation; do not weaken those gates.
- If ARAP converges but does not visibly improve the native sprite, stop solver
  tuning and test one pose-specific elbow corrective layer.
- If the regular active mesh folds despite valid constraints, replace it with one
  authored contour cage; do not sweep mesh spacing indefinitely.
- If ARAP improves shape but cannot reach the area target for a visually valid
  reason, record that evidence and revise the metric explicitly before any
  second-arm work.
- Do not train or fine-tune a neural parser during this experiment.

## Deliverables

Expected repository changes:

- a focused C++ ARAP artwork library and public contract;
- platform-neutral ARAP unit tests;
- active-alpha mesh construction and constraint selection;
- an ARAP deformation option in the layered-puppet proof tool;
- direct linear-versus-ARAP review publication;
- updated mouse experiment specification;
- measured findings and active handoff updates.

Generated images and model output remain gitignored evidence. No Python
environment or model weights enter the repository.

## Path after a pass

1. Apply the accepted ownership, underpaint, active mesh, and deformation method
   to `handwear-r`.
2. Split the accepted footwear layer into left and right boots.
3. Obtain complete left/right leg and tail layers through targeted completion or
   authored correction.
4. Bind each complete leg to hip/knee/foot and run the same A/B-quality gates.
5. Run full-character neutral/contact/passing/airborne acceptance.
6. Author and import the six replacement idle, run, and airborne clips.
7. Record live transition acceptance, then unblock Runtime M4.

## Review questions

1. Is ARAP complexity justified before trying one authored elbow corrective?
   The recommendation is yes: one bounded A/B run will establish whether the
   complete layered skeleton can generalize to both arms and legs.
2. Are the ownership, neutral, connectivity, joint, inversion, determinism, and
   native visual gates sufficient?
3. Is 95% retained area the right initial shape-preservation threshold for these
   in-plane poses?
4. Should the first ARAP implementation remain a regular active grid, or should
   review require an authored contour cage immediately?

The recommended decision is to approve the bounded regular-active-grid ARAP A/B
experiment. It reuses every accepted input and changes only the deformation
solver, so its result will isolate the remaining question cleanly.
