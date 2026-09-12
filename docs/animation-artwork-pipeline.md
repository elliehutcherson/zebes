# Animation frame-set pipeline

Frame-set processing, retained recipes, transactional bundle lifecycle, and
headless curation are implemented. Character art follows the
[3D mouse plan](mouse-3d-plan.md); previous generation and layered-part gates
are superseded.

## Current contract

Imported or manually authored sheets enter one deterministic offline processing
run. The runtime consumes ordinary Texture/Sprite resources and Blueprint state
bindings. Remote animation generation is excluded from the production path.

Each clip declares source cells/rectangles, frame count, one logical canvas,
shared origin/contact line, ordered durations in simulation ticks, loop or
hold-last playback, and bounded pixel/byte dimensions. Registration may translate
to the authored origin; per-frame auto-fit is invalid. Collider geometry remains
independently authored Blueprint behavior.

[Runtime architecture](architecture/runtime.md#player-and-animation) owns stable
Blueprint state keys, boot-resolved handles, playback resets, facing, and the
production player's exact collider. Recipes may change only explicitly owned
state Sprite bindings, not colliders, unrelated states, or level placements.

## Processing and persistence

The platform-neutral pipeline in `src/artwork/animation_frame_set_pipeline.*`
operates on copied RGBA values without API, filesystem, SDL, or manager access:

1. Validate storage, dimensions, source geometry, occupied cells, and frame count.
2. Extract declared cells and isolate alpha or the configured matte.
3. Register every frame to the shared canvas/origin, then apply one scale and
   pixel-block policy in premultiplied-alpha space.
4. Resolve one palette and deterministic quantization for the whole clip.
5. Enforce alpha, component, border, nonempty-frame, and drift requirements.
6. Pack deterministically and emit complete ordered Sprite frame metadata.

`AnimationFrameSetRecipe` retains source IDs/digests, target Blueprint and state
bindings, per-clip layout/timing/geometry, pipeline settings/version, generated
resource IDs, and accepted output snapshots. Loaders are strict and versioned.

Preparation returns pixels, definitions, binding changes, recipe data, and
diagnostics. Commit checks source/target/output snapshots and all references
before publishing Texture/Sprite resources, recipe, and Blueprint bindings.
Failures compensate in reverse order and report rollback failures.

Regeneration preserves resource IDs and updates only recipe-owned data after
snapshot validation. Deletion refuses external references and removes the complete
owned graph. Shared source retention and transaction boundaries are documented
in [resources and data](architecture/resources-and-data.md).

## Review

Use the existing [headless frame-set review](headless-curation.md#reviewing-an-animation-frame-set-against-its-recipe)
and `import_animation_frame_sets` tool. Review native frames, enlarged contact
sheets, ordered strips, origin/contact/bounds overlays, adjacent differences,
loop closure, and hold-final evidence. Persisted re-review must be byte-stable.

## Remaining work

- Produce convincing character clips under the 3D plan, then import the
  production set and visibly exercise idle, locomotion, facing, jump/fall, and
  landing transitions in Catacombs.
- Add editor controls for imported sheets, clips, timing, origin, and state
  binding using the existing retained-source lifecycle. No remote animation
  generation is needed.
- Keep runtime M4 sequenced by the roadmap after the character milestone.

## Verification

Focused tests cover bounds/frame count, registration, shared palette/alpha,
deterministic packing, timing, strict recipes and shipped migrations, stale
snapshots, and compensation at each persistence step. Integration changes also
exercise affected Sprite, Blueprint, loaded-level, runtime, and curation tests.
The user's visible art verdict remains separate from software validation.
