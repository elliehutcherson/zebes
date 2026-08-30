# Editor and authoring architecture

## Model and panel boundary

Stateful editors separate behavior from presentation:

- models own drafts, selection, dirty state, validation, pending work, and
  transaction intents;
- panels render copied model state and report user intents;
- editor coordinators start background/provider work and commit through `Api`;
- renderers and texture stores own native resources.

Models and panels do not own provider clients, filesystem workers, `SDL_Texture`,
or manager pointers. Platform-neutral tests exercise model behavior without an
SDL window.

## Draft lifecycle

Opening an asset snapshots the managed definition into a draft. Editing mutates
only the draft. Save validates and commits explicitly; revert restores the
managed snapshot; closing a dirty draft requires a decision. Dirty comparison
runs at the leave/save boundary, not every frame.

Cross-editor navigation carries stable IDs. The receiving editor resolves a
fresh definition each frame or on activation; no pointer crosses a catalogue
refresh.

## Level authoring

A level draft owns ordered `WorldLayer` values, parallax zones, spawn/world
geometry, and one tileset ID. Setup and zone creation are separate transactions.
Invalid new drafts cannot publish; existing invalid definitions may open for
repair.

`ViewportTab` converts canvas input into platform-neutral interaction values.
Controllers own mode priority, continuous drag/paint state, and discrete
results. Scene composition receives copied immutable resource snapshots and
never retains manager pointers.

Palettes identify selection by stable resource ID and resolve transient objects
for display. Missing referenced artwork is an explicit error or visible
placeholder according to the authoring contract; it is never silently hidden.

## Artwork authoring

Imported and generated prop/parallax workflows share:

- bounded source ingestion;
- retained `SourceArtwork`;
- deterministic typed processing stages;
- candidate review before commit;
- pure preparation;
- transactional bundle creation/regeneration/deletion;
- explicit provenance and artifact publication.

Remote generation is optional source acquisition, not a runtime dependency.
Generated animation is deprecated; animation editor work accepts imported or
manual frame sheets only.

Background workers receive copied immutable inputs and return values/events.
The editor thread alone mutates managers and GPU state. Cancelling a request does
not invent completion; the engine still retires its operation exactly once.

## Anchors and geometry

Entity position is an authored origin. Sprite frame render offsets place visible
pixels relative to that origin; atlas coordinates only select pixels. Collider
and attachment geometry are independent from visual bounds.

Prop attachment modes are grounded, ceiling, or explicitly free. Anchor gizmos
share one platform-neutral geometry path across Blueprint, Prop, Level, and
Sprite authoring.

## Error handling

An editor transaction either succeeds completely or leaves the managed graph
unchanged. Failures surface through the uniform editor notification boundary.
Do not keep a partially created output, silently select another resource, or
save a dependent draft as a side effect of saving its owner.

## UI verification

Pure models receive deterministic tests. SDL/ImGui tests verify wiring and
visible contracts. Human gates are required for composition, motion, seam,
palette, and readability decisions that source-level assertions cannot make.
