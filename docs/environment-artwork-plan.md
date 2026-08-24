# Environment artwork and parallax plan

**Status: Milestones 0, 1, 1.5, 1.6, and 2 are accepted. The import-first
background artwork pipeline, retained source, deterministic recipe, managed
bundle lifecycle, editor workflow, and cave-plate acceptance gate are complete.
Milestone 3, generated background candidates, is next.**
Written 2026-08-22 for the first cave vertical slice, revised the same day to
make standalone parallax-theme ownership the highest-priority implementation
milestone, and updated 2026-08-23 after accepting composed parallax layers.
Update milestone states here as work lands; use [`roadmap.md`](roadmap.md) only
for the higher-level sequence.

## 1. Goal

Build a coherent environment-artwork workflow that can produce and author:

- camera-relative parallax backgrounds;
- world-relative background props behind gameplay;
- gameplay terrain, hazards, and solid props;
- sparse foreground props in front of gameplay.

The target is a readable tile-driven platformer with several visual depth
planes. It is not a painterly Hollow Knight-style pipeline, a generic scene
graph, or a promise that one remote generation call will produce a finished
multi-layer environment.

The first acceptance scene is a short cave level using the existing generated
`lucinda_cave` terrain and Cave Crystal prop, plus a small reusable cave
environment kit.

## 2. Existing contracts to preserve

This work extends existing boundaries; it does not replace them.

### 2.1 The two layer systems remain distinct

`ParallaxLayer` and `WorldLayer` solve different problems:

| Concern | Parallax layer | World layer |
|---|---|---|
| Ownership | A standalone `ParallaxTheme` resource | The level |
| Coordinates | Camera-relative | World-relative |
| Content | Ordered texture elements, finite or repeated as one composition | Sparse tiles and placed entities |
| Collision | Never | Defined by each tile or entity |
| Selection | Theme chosen through a zone | Always present; one active for editing |
| Intended use | Distant environmental planes | Back decor, gameplay, and front decor |

The frame composition for the first level remains:

```text
parallax theme, far to near
  -> Back Decor world layer
  -> Gameplay world layer
  -> Front Decor world layer
  -> editor-only overlays
```

All parallax remains behind world content. Background and foreground *props*
are the same Texture/Sprite/Blueprint assets placed in different world layers.
There is no `ForegroundProp` asset type and no automatic layer assignment.

True foreground parallax is deferred until a real asset needs to move relative
to the camera while also drawing in front of gameplay. A foreground prop fixed
in world space does not justify another composition band.

### 2.2 Existing asset roles remain narrow

- `Texture` is managed runtime pixel content.
- `Sprite` selects and sizes frames from a texture.
- `Blueprint` describes placeable states and optional collision.
- `SourceArtwork` is retained authoring input and provenance.
- `PropRecipe` deterministically rebuilds an anchored prop bundle.
- `ParallaxTheme` is a reusable resource that assembles already-created
  textures independently of any level.

A background does not become a Sprite or Blueprint merely to enter a level.
Each processed output remains a whole managed Texture referenced by a
`ParallaxElement` inside a layer.

### 2.3 Infrastructure is shared, coordinators are not

The background workflow must reuse:

- `ImageGenerationEngine`, provider adapters, credentials, cancellation, and
  candidate provenance;
- `RgbaImage`, bounded image decoding, PNG writing, and image digests;
- the resolved terrain palette and reusable image-transform primitives;
- `SourceArtworkManager`, after its prop-specific path and limit names are
  generalized;
- API preflight, compensated bundle creation, reference scanning, and deletion
  patterns.

It must not route a background through `PropArtworkPipeline`. That coordinator
assumes one isolated anchored subject, while a background needs coverage,
alpha-role, and repetition diagnostics. Share lower-level image operations,
not incompatible workflow semantics.

Do not create another HTTP transport, provider session, retained-source store,
texture store, generic workflow graph, or third layer hierarchy.

## 3. Authored environment model

### 3.1 Recommended world layers

The standard first-level setup is stored back to front:

1. `Back Decor`: non-colliding world props behind gameplay.
2. `Gameplay`: collision terrain, hazards, pickups, solid props, and actors.
3. `Front Decor`: non-colliding, deliberately sparse occluders.

The existing `Base` layer may be renamed `Gameplay`; the author adds the other
two with the existing world-layer operations. Visibility and locking remain
transient editor state. A setup shortcut can be added only after repeated use
shows that three manual operations are a real burden.

### 3.2 Recommended parallax theme

One cave theme should begin with three depth roles stored far to near:

1. `Far Fill`: opaque, very slow, covering the complete camera view.
2. `Far Formations`: distant silhouettes, usually transparent over the fill.
3. `Near Background`: several composed arches, columns, roots, or crystal
   elements, still behind every world layer.

Suggested scroll factors are authoring presets, not domain constraints:

| Preset | X | Y |
|---|---:|---:|
| Far | 0.05 | 0.05 |
| Middle | 0.20 | 0.10 |
| Near background | 0.50 | 0.25 |

The renderer continues to accept other finite values. Values outside `[0, 1]`
receive an authoring warning because they normally describe fixed UI, reverse
motion, or a plane closer than the camera rather than background depth.

### 3.3 Themes are standalone resources

Milestone 0 removed the former map of integer-keyed themes from every `Level`.
Parallax authoring, recipes, and generated background work build on the
standalone ownership model below.

The target model is:

```cpp
struct ParallaxTheme {
  std::string id;
  std::string name;
  std::vector<ParallaxLayer> layers;
};

struct ParallaxZone {
  // Zone-local identity, bounds, and fade settings remain here.
  std::string theme_id;
};

struct Level {
  // World geometry and zones remain; the embedded theme map is removed.
  std::vector<WorldLayer> layers;
  std::vector<ParallaxZone> zones;
};
```

Add a `ParallaxThemeManager` backed by
`assets/definitions/parallax_themes/`. It owns strict load, create, update,
duplicate, lookup, listing, and deletion preflight. Theme IDs use the same
string resource identity convention as textures, sprites, and levels. A zone
references one theme resource by ID; it never owns a mutable theme copy.

Generated background recipes still own output textures, not themes, zones, or
level records. A dedicated Theme Editor assembles those textures. This keeps
artwork production reusable and prevents a recipe regeneration from silently
changing level composition.

Do not retain a permanent hybrid of embedded and standalone themes. Supporting
both would duplicate lookup, validation, selection, save, reference, deletion,
and preview paths. The old form exists only as migration input.

### 3.4 Theme migration

Each existing `(level ID, local integer theme ID)` pair becomes one standalone
theme definition. Migration must not deduplicate structurally identical themes:
two equal embedded values may represent intentionally independent art direction.
An author can consolidate them later through explicit zone reassignment.

Migration performs these steps:

1. Preflight every old level and every output path before changing a file.
2. Derive a deterministic theme UUID from the level ID and old local theme ID,
   so retrying after interruption addresses the same resource.
3. Write a theme named `"<level name> — <theme name>"`, preserving layer order
   and contents exactly.
4. Rewrite each zone's integer `theme_id` to the new resource UUID.
5. Remove the embedded `themes` collection from the level.
6. On rerun, accept an already-written theme only when its complete contents
   match the expected migration output; refuse a conflicting file.

A level containing both the embedded collection and new string references is
half-migrated and fails unless the migration tool can prove the deterministic
resource outputs match before completing the rewrite. A zone naming a missing
old local theme fails before any write. Shipped-definition tests load every
new theme and every rewritten level.

## 4. Background artwork resource design

### 4.1 Generalize retained source artwork once

`SourceArtwork` already has the correct identity, digest, dimensions, and
imported/generated provenance. Generalize it rather than duplicating it:

- **Implemented.** Rename `PropSourceLimits` at the storage boundary to
  `SourceArtworkLimits`; prop configuration continues embedding an instance.
- **Implemented.** Move retained images from the prop-specific directory to
  the neutral ID-backed path `source_art/<source-id>.png`.
- **Implemented.** Add a strict schema migration that moves each ID-backed PNG
  and rewrites its definition without overwriting an existing target.
- let prop and parallax recipes both reference the same source;
- delete a source only after reference scans find no recipe of either kind.

The migration must load every shipped definition and must be idempotent. A
half-migrated definition or conflicting source/target file fails rather than
guessing which image is authoritative.

### 4.2 One recipe produces one texture

**Implemented.** A versioned `ParallaxArtworkRecipe` lives under
`assets/definitions/parallax_artwork_recipes/`. One recipe owns one processed
Texture. A theme can combine several recipe outputs, and one output can be used
by several theme layers or levels.

Conceptually the recipe contains:

```text
identity
  id, name, schema_version, pipeline_version

input
  source_artwork_id
  optional terrain_recipe_id
  resolved artwork-style snapshot

processing
  target pixel width and height
  crop/fit policy
  nearest-neighbour raster policy
  palette quantization policy
  alpha role: opaque plate or transparent overlay
  overlay extraction: preserve alpha or remove a configured flat matte,
  including enclosed openings containing that matte
  repeat diagnostics requested for X and/or Y

output authority
  texture_id
  expected width and height
  final pixel digest
```

Scroll factor, element scale and position, layer offset and repeat period, and
layer/element order do **not** belong in the recipe. They are
placement/composition decisions owned by `ParallaxLayer` inside the standalone
theme. One recipe continues to produce one reusable Texture; Milestone 1.6
lets one layer compose several such outputs without baking them into an
oversized image. The artwork editor may suggest composition defaults after
creation, but there is only one serialized authority for each value.

### 4.3 Bundle lifecycle

Creation publishes the generated Texture and then its recipe. Preflight checks
all IDs, names, references, dimensions, and output pixels before the first
write. Failure removes already-created members in reverse order.

Regeneration starts from retained `SourceArtwork`, reproduces the processed
pixels locally, checks that the recipe and output snapshots are still current,
and replaces only the owned texture pixels and digest. It never makes a remote
request silently.

Deletion preflights every external reference to the output texture. A texture
used by a parallax layer, sprite, tileset, or unrelated recipe prevents bundle
deletion and names the referrer. The source is deleted last only when no prop
or parallax recipe still uses it.

## 5. Deterministic background pipeline

The background coordinator is a platform-neutral sequence with typed stage
diagnostics:

1. **Validate source:** decoded dimensions, byte/pixel limits, and digest.
2. **Frame:** apply the explicit crop/fit policy to the target aspect.
3. **Rasterize:** use nearest-neighbour operations and the selected pixel
   profile; no implicit filtering.
4. **Quantize:** optionally use the complete resolved terrain palette so
   terrain, props, and background share an authored color language.
5. **Apply alpha policy:**
   - an opaque plate must finish with alpha 255 for every pixel;
   - an imported transparent overlay may preserve meaningful source alpha;
   - an opaque generated overlay may remove a configured flat matte before
     continuing; tolerant exterior cleanup remains border-connected, while an
     enclosed opening is removed only when it contains a configured-matte
     core, preserving isolated merely similar foreground colours and every
     non-matte component even when the artwork intentionally touches an edge;
   - every overlay must finish with at least one visible pixel.
6. **Build repetition review:** create wrapped 3-by-1 and/or 1-by-3 previews
   and report opposing-edge difference statistics.
7. **Validate output:** exact dimensions, bounded pixels, final digest, and
   non-empty visible content.

The pipeline does not use the complete prop subject-isolation stage. An opaque
scenic plate is not a foreground subject, and a silhouette that intentionally
touches an image edge violates the prop coordinator's largest-subject and
component assumptions. Flat-matte removal may share its lower-level color
distance and border flood-fill primitive, but background extraction has its
own configuration, validation, and preview and never discards a component for
being smaller than a presumed primary subject.

Seam quality remains a human visual decision in the first version. Exact first
and last columns are neither necessary nor sufficient for a good repeating
image, while an arbitrary difference threshold would encode an unsupported
claim about what the eye forgives. The tool must make seams easy to see and
must never label an image “seamless” merely because a heuristic passed.

## 6. Validation contract

Validation is split by ownership. Intrinsic definition validation must not
reach into managers; catalog and visual-readiness checks happen at API/editor
boundaries that have the needed resources.

### 6.1 Hard intrinsic definition errors

Add `ValidateParallaxTheme` so a resource cannot be published with:

- an empty theme ID, theme name, layer collection, layer name, or texture ID;
- non-finite scroll factors or offsets;
- a non-finite or non-positive base scale;
- duplicate theme identity in the catalog.

Extend `ValidateLevel` so it rejects:

- an empty or malformed zone theme resource ID;
- non-finite or negative zone fade lengths;
- a fade where `2 * fade_x` exceeds zone width or `2 * fade_y` exceeds zone
  height;
- all existing invalid zone IDs, bounds, world-layer IDs, and duplicate
  identities.

The Theme Editor may hold an incomplete draft with an empty layer texture, but
the manager does not publish it. Every saved theme is runtime-complete.
Duplicate display names remain valid at the domain level because names are not
identity; the catalog/editor must disambiguate them with IDs and may warn.

The UI must clamp or refuse invalid typed values before save where practical,
but domain validation remains authoritative. The roadmap now records the
current state accurately: `fade_length` is not validated today.

### 6.2 Hard recipe and bundle errors

Reject before writing when:

- the source, optional terrain recipe, or output texture snapshot is missing;
- schema or pipeline versions are unknown;
- source/output dimensions, pixels, or digests disagree;
- target dimensions or image limits are invalid;
- an opaque plate contains transparency;
- a transparent overlay contains no visible pixels;
- requested matte removal cannot identify and clear any pixels matching its
  stored matte configuration;
- a repeated/non-repeated coverage configuration cannot be represented by the
  current renderer;
- a resource ID or name collides;
- creation or regeneration would overwrite content not owned by the recipe.

### 6.3 Catalog validation

At the `Api` save/preflight boundary:

- every theme-layer texture ID must name a managed Texture;
- every zone theme ID must name a managed ParallaxTheme;
- recipe source, terrain-style, and output IDs must resolve;
- theme deletion must refuse while any level zone references it;
- texture deletion must scan standalone themes instead of embedded levels;
- texture and source deletion scans must include both prop and parallax recipes;
- a generated-background bundle must be internally complete before its recipe
  becomes visible.

`LevelManager` remains free of TextureManager and other catalogs so levels can
be parsed and migrated independently. Catalog validation belongs in `Api`, not
in the serialized object reader.

### 6.4 Authoring warnings

Warnings do not block saving drafts:

- background scroll factors outside `[0, 1]`;
- an opaque-looking overlay or a transparent far-fill plate;
- a non-repeating axis that does not cover the configured camera view and
  zoom range at its authored scale;
- conspicuous opposing-edge differences in a repeated texture;
- themes not used by any level zone, zones that leave intended camera routes
  uncovered, or layers with no texture;
- overlapping zones whose priority may surprise an author;
- foreground props covering designated gameplay-readable regions.

Warnings must report measured facts and show previews. They must not claim
subjective art is invalid.

## 7. Theme and Level Editor refactor

Theme extraction changes the editor ownership boundary substantially. Do not
leave catalog-owned theme values editable through a `Level&` merely to preserve
the current panel layout.

### 7.1 Standalone Theme Editor

Add a `Parallax Theme` asset editor following the existing snapshot-based
editor pattern. A platform-neutral `ParallaxThemeEditorModel` owns one draft,
its saved snapshot, selected layer index, and tested operations for:

- new, open, rename, duplicate, save, and delete theme;
- add, rename, reorder, and delete theme layers;
- selection reconciliation when a layer is reordered or deleted;
- applying Far, Middle, and Near-background presets;
- resolving texture choices by stable texture ID each frame;
- reporting every level zone that references the open theme.

Edits affect no level or catalog entry until explicit Save. Saving a shared
theme intentionally changes every referencing level's rendered environment
without making those levels dirty; their zone references did not change.
Duplicate creates a new resource and is the expected way to make a level-local
variant. Deletion uses `ConfirmPrompt` and API reference refusal.

Parallax layers currently have no persistent ID and nothing outside the theme
references an individual layer. Reordering can therefore remain an atomic
vector swap plus selected-index update; do not add a serialized ID until a
second persistent consumer requires one.

The Theme Editor provides:

- searchable texture thumbnails rather than a stale creation-time cache;
- explicit Move Nearer and Move Farther controls;
- offset controls in the layer inspector, where the value is actually owned;
- complete-theme and selected-layer preview modes;
- an optional read-only level/zone context picker for preview, resolved by ID
  without borrowing or mutating a `Level`;
- a repetition preview and camera-travel scrub;
- an action to open/create Parallax Artwork without duplicating texture import
  logic.

### 7.2 Reduced Level Editor responsibility

Remove theme and parallax-layer mutation from the Level Editor navigator and
selection state. The Level Editor continues to own:

- world layers and entities;
- zones, zone bounds, fade settings, and zone selection;
- a searchable catalog picker assigning a theme resource to a zone;
- active-zone and selected-zone theme preview;
- an `Edit Theme` action that opens the referenced asset in Theme Editor;
- a `Duplicate and Assign` action for making an independent variant safely.

`ViewportTab` receives resolved immutable theme definitions for the frame. It
does not retain manager pointers or mutable catalog objects. Saving a Level
writes zone theme IDs only and can never save a theme draft as a side effect.

### 7.3 Cross-editor navigation and ownership

`EditorUi` remains the composition root and routes stable-ID navigation
requests between editors. Level Editor must not own or call Theme Editor
directly. It emits requests such as `OpenTheme(theme_id)` and
`DuplicateThemeAndAssign(zone_id, theme_id)`; `EditorUi` invokes the API,
opens/reconciles the destination editor, and returns the resulting resource ID
to the still-open level draft.

Duplicate-and-assign preflights the selected zone and source theme, creates the
new theme resource, then changes the zone ID in memory. If resource creation
fails, the level draft is unchanged. Once creation succeeds, assignment cannot
perform catalog work or fail ambiguously. The duplicate is an explicitly
created asset and remains in the catalog if the author later discards the level
edit; unused-theme reporting makes that state visible.

Editors exchange IDs and copied snapshots, never raw theme pointers or shared
mutable drafts. Catalog refreshes reconcile selection by ID. This preserves
the existing lifetime boundary and prevents tab order or editor destruction
from determining theme validity.

Until fade rendering is implemented, the zone inspector must label fade values
as unsupported or disable them. Editable controls must not imply a visible
effect that does not exist.

### 7.4 Level Editor authoring interaction model

The first Milestone 1 content-gate attempt exposed a discoverability failure,
not an ownership failure. At the time, a new level could be saved at zero width
and height; that disabled zone creation, while the theme picker existed only in
the inspector of a selected zone. The UI therefore hid the required sequence
across three selections and communicated it mainly through a disabled button.
Theme assignment remains zone-owned; Milestone 1.5 makes the workflow explicit.

The target Level Editor layout is:

```text
level toolbar: identity, save/close, dirty state, actionable errors
  -> level contents: level settings, world layers, and parallax zones
  -> dominant world viewport
  -> contextual inspector with a selection breadcrumb
  -> collapsible/resizable placement palette
```

This is a focused interaction refactor, not a generic docking framework. Keep
the existing `LevelPanelModel`, `WorldLayerModel`, `ViewportTab`, palette
panels, zone-owned theme references, and cross-editor stable-ID requests. Do
not add `Level::theme_id`, edit theme layers through a `Level&`, or duplicate
Theme Editor state.

The interaction contracts are:

- **New-level setup is an explicit draft step.** Choosing New starts an
  unsaved level draft rather than immediately publishing a zero-sized level.
  A setup surface names the level and labels world width, world height, tile
  render size, tileset, and spawn clearly. Applying setup mutates only the
  draft; Save/Create is the persistence boundary.
- **Invalid prerequisites are actionable.** When world bounds are not positive
  and tile-aligned, the viewport and Parallax section both link to Level Setup
  instead of presenting an unexplained disabled operation. The editor must not
  persist a new zero-area level. Loading any historical zero-area level remains
  possible so it can be repaired, but its inspector must show the exact save
  blocker.
- **The hierarchy describes ownership.** Use `World Layers` and `Parallax
  Zones` as sibling collections below one Level Settings item. A level is one
  authored world, not a collapsible collection of scenes, so these sections
  remain visible rather than hiding their controls behind disclosure arrows.
  Rename the action to `Add Parallax Zone…` and show every row as `zone name —
  assigned theme`; missing or unresolved themes use an explicit error label
  rather than a blank value.
- **Zone creation is transactional.** `Add Parallax Zone…` opens a transient
  creation draft initialized to the complete level bounds. Name, theme, and
  valid bounds are required before Commit appends it to `Level::zones`.
  Cancel leaves the level draft untouched. A successful commit selects and
  frames the new zone.
- **Theme assignment is present at the point of use.** The zone-creation and
  zone-detail inspectors use one searchable, stable-ID theme catalog picker.
  The saved theme name is visible in the hierarchy. `Edit Theme` and
  `Duplicate and Assign` remain cross-editor requests and never save the level
  implicitly.
- **Inspector context is unmistakable.** The right rail begins with a
  breadcrumb such as `Level > Parallax Zone > Cave Entrance`, followed by the
  relevant properties. Level persistence has one Save action in the global
  toolbar; contextual inspectors do not duplicate it. All inspectors use the
  shared two-column property grid: permanent labels and units occupy the left
  column, full-width editable controls occupy the right, and hidden widget IDs
  prevent ImGui labels from being clipped after an input field.
- **Readiness has one presentation path.** Save/Create is disabled when the
  shared readiness result has blockers, and `Review N issues` selects Level
  Settings where the complete actionable list is shown. The viewport and
  hierarchy may provide a compact route to Level Settings, but they do not
  duplicate the detailed blocker list.
- **Preview mode is explicit.** Near the viewport, label parallax preview as
  `Active Zone`, `Selected Zone`, or `Off`; selecting or creating a zone makes
  `Selected Zone` the temporary authoring view. The viewport continues to
  resolve immutable theme snapshots and does not retain catalog pointers.
- **The palette follows readiness and mode.** Hide or clearly disable placement
  content until a tileset and active world layer make it usable. Preserve the
  shared blueprint/tile/terrain palette implementations rather than rebuilding
  their asset presentation.

The shortest intended human workflow becomes: New Level → complete Level
Setup → Create Level → Add Parallax Zone → choose Cave Theme and bounds →
Create Zone → preview Selected Zone → Save. No documentation should be required
to discover the theme picker.

## 8. Generated-source editor flow

Add a `Parallax Artwork` editor surface rather than expanding Prop Artwork with
mutually incompatible controls. It reuses provider-neutral infrastructure but
has background-specific prompts, review, and output settings.

When this becomes the second image-generation UI, extract only the common
request lifecycle from Prop Artwork: provider selection, submit/cancel,
request-ID ownership, candidate navigation, and accept/discard state. Prop and
parallax editors keep separate prompt defaults and processing models.

The default generated-background instructions must state one layer's role,
target aspect, palette/style direction, whether it is an opaque plate or an
overlay, and whether horizontal repetition is intended. When the selected
provider cannot return transparency, overlay instructions request a flat,
contrasting matte for deterministic extraction. They must not reuse the prop
instruction that asks for one isolated uncropped object.

Provider acceptance retains the chosen raw candidate as `SourceArtwork` with
existing provenance. Imported and generated candidates then enter the same
deterministic background pipeline. A remote request never writes directly to a
runtime Texture or Level.

Current providers do not accept reference images. The first version therefore
generates one layer at a time and makes coherence through shared instructions,
target geometry, and deterministic palette processing. Do not advertise
automatic coherent multi-layer generation until an adapter can actually use a
reference plate and a visual check proves it.

## 9. Zone fading and rendering

Zone fades are independent of background generation and do not block a
single-theme first level. Implement them after imported and generated theme
assembly works.

The first fade contract supports blending at most two themes across one shared
horizontal or vertical zone edge:

- a vertical shared edge uses each zone's `fade_x` inward width;
- a horizontal shared edge uses each zone's `fade_y` inward width;
- zero on both sides preserves the current hard cut;
- the blend spans the two inward widths and is continuous at the boundary;
- zones that overlap by area retain the existing later-zone priority and may
  not request a fade through that overlap;
- intersecting horizontal and vertical fade bands that would require three or
  four themes are rejected by the environment-readiness validator until the
  compositor explicitly supports that case.

Replace the one-zone result with a platform-neutral result containing primary
theme, optional secondary theme, and a normalized blend weight. The viewport
resolves both texture sets and the renderer draws them with explicit batch
opacity. Native tint/alpha values remain inside the renderer adapter.

Headless tests must pin weights on both sides of an edge, unequal inward fade
widths, half-open boundary behavior, missing neighbors, overlap priority, and
unsupported corners.

Authorable per-layer tint, opacity, blend mode, autoscroll, independent repeat
period, and foreground parallax remain separate follow-ups. Add one only when
the cave content demonstrates a need that cannot be baked into its texture.

## 10. Implementation milestones

Each milestone must leave the editor usable and must land with its focused
tests. Do not start remote generation before the imported workflow proves the
output model.

### Preparation — documentation archive and plan

- Archive completed historical design documents under `docs/history/`.
- Record this plan and link it from the roadmap and README.

This preparation is complete. It does not satisfy the first implementation
milestone.

### Milestone 0 — extract themes and separate UI ownership (implemented)

- **Implemented.** Add string-identified `ParallaxTheme` resources, strict serialization,
  `ParallaxThemeManager`, API operations, and asset-catalog participation.
- **Implemented.** Remove the embedded theme map from `Level`; make zones reference resource
  IDs and update intrinsic/catalog validation boundaries.
- **Implemented.** Implement the deterministic, idempotent multi-file migration without
  deduplicating themes.
- **Implemented.** Move theme/layer draft editing into the standalone Theme Editor.
- **Implemented.** Reduce the Level Editor to zone assignment, theme usage, contextual preview,
  `Edit Theme`, and `Duplicate and Assign`.
- **Implemented.** Update texture/theme deletion scans and shipped-asset validation.

Acceptance: every shipped level renders identically after migration; one theme
can be assigned to zones in two levels; editing and saving it updates both;
duplicating and reassigning isolates later edits; deleting a referenced theme
or one of its textures is refused without partial mutation.

### Milestone 1 — harden imported parallax authoring and run the content gate

- **Implemented.** Add remaining intrinsic geometry/fade validation and catalog
  texture-reference checks.
- **Implemented.** Add layer reorder, selection reconciliation, presets, live texture discovery,
  thumbnails, correct offset ownership, repetition diagnostics, and camera
  coverage diagnostics in Theme Editor.
- **Implemented.** Give Theme Editor a persistent far-to-near hierarchy,
  dominant aspect-correct logical game viewport, independently scrolling
  inspector, and collapsible diagnostics drawer. Level/zone preview routes are
  resolved as reachable camera centers at each zoom rather than raw world
  corners.
- **Implemented.** Mark fades unsupported in Level Editor until Milestone 5.
- **Accepted human content gate.** Assemble one temporary three-plane cave background through the existing
  Texture, Theme, and Level editors.
- **Accepted human content gate.** Preview it behind existing cave terrain and the Cave Crystal prop.
- **Accepted human content gate.** Record target viewport, zoom range, texture sizes, desired repetition, and
  observed seam/coverage problems before choosing processing defaults.

Run and record that review in
[`cave-parallax-content-gate.md`](cave-parallax-content-gate.md). Automated
tests cannot establish that the depth split is visually useful.

Acceptance: a human can import three hand-authored PNGs, assemble and reorder a
standalone theme, assign it to a zone, save, reopen, and get the identical
composition. One camera view also establishes that the proposed
far/middle/near division produces useful depth without foreground parallax.

### Milestone 1.5 — make Level Editor environment authoring discoverable (implemented)

This milestone implements the interaction model in §7.4 before the cave gate
continues. It deliberately preserves the ownership boundaries established in
Milestone 0.

1. **Implemented — separate New from Create.** Change the level catalog action
   to open an unsaved draft. Add tested draft lifecycle events for setup,
   create/save, cancel/close, and failed persistence without partial catalog
   mutation.
2. **Implemented — add a platform-neutral readiness model.** Derive
   categorized facts rather than one overloaded ready flag: save readiness
   covers identity, bounds, tile alignment, spawn, and zone references;
   placement readiness adds tileset and active-layer requirements;
   parallax-zone readiness adds valid world bounds and theme-catalog
   availability. UI code renders these facts but does not invent a second
   validation policy.
3. **Implemented — rebuild the Level Editor shell.** Add a full-width toolbar,
   fixed scene hierarchy rail, dominant viewport, contextual inspector with
   breadcrumb, and collapsible/resizable palette. Preserve independent
   scrolling and make dirty/error state visible without consuming the
   hierarchy.
4. **Implemented — explicit Level Setup.** Label world dimensions and tile size,
   provide the required tileset/spawn inputs, frame the configured world, and
   route blocked hierarchy actions back to setup. Prevent new zero-area levels
   from being published; allow existing invalid drafts to be repaired.
5. **Implemented — transactional zone creation.** Add a small platform-neutral
   zone creation model with full-level default bounds, required theme
   selection, validation, cancel, commit, unique stable IDs, automatic
   selection, and viewport framing.
6. **Implemented — make parallax use visible.** Present `Parallax Zones`
   alongside world layers, show assigned theme names in rows, add the searchable
   theme picker to create/detail flows, and expose explicit Active/Selected/Off
   preview modes. Keep Edit Theme and Duplicate and Assign routed through
   `EditorUi`.
7. **Implemented — gate the palette by authoring readiness.** Retain existing
   palette panels while preventing placement controls from appearing usable
   before setup, tileset selection, and active-layer selection are complete.
8. **Implemented — validate at stable boundaries.** Prefer headless tests for
   readiness, draft lifecycle, zone-creation transactions, stable-ID catalog
   filtering, and preview-mode reconciliation. Keep ImGui tests to event wiring
   and one focused smoke path; do not add screenshot-coordinate tests.
9. **Implemented — finish the inspector and hierarchy usability pass.** Add a
   reusable two-column property grid with permanent labels, units, concise
   field help, and full-width controls; migrate level, layer, zone, zone-create,
   and entity properties to it. Replace the false collapsible scene root with
   always-visible Level Settings, World Layers, and Parallax Zones. Consolidate
   save blockers behind `Review N issues`, hide the placement palette until it
   is usable, expose Frame World beside world dimensions, and label valid or
   fractional tile-grid dimensions directly.
10. **Accepted human gate — run the workflow.** Create a disposable cave level
   without written instructions, assign the existing Cave Theme, save/reopen,
   and verify the same zone, theme, bounds, hierarchy selection, and
   selected-zone preview. Then resume the remaining Milestone 1 content gate.

Focused verification includes `level_panel_model_test`, the new readiness and
zone-creation model tests, `parallax_zone_panel_test`, `level_editor_test`,
`viewport_tab_test`, `level_test`, API catalog validation, and loading every
shipped level definition. Use `scripts/test.sh --affected-target` for changed
Level Editor targets; run the complete UI merge-gate command only if the shell
or shared serialized validation boundary makes the affected set genuinely
broad.

Acceptance: starting from the level catalog, an author can discover and
complete setup, add a valid parallax zone, assign an existing theme, see that
assignment in both hierarchy and viewport, save, close, and reopen without
consulting documentation. No failed or canceled step publishes a partial
level or zone, and the Level Editor still cannot mutate theme layers.

### Milestone 1.6 — compose multiple elements inside one parallax layer

**Accepted 2026-08-23.** Schema version 2 owns ordered
stable-ID elements and explicit repeat periods. The deterministic migration
materialized every shipped one-texture layer without changing its scaled repeat
geometry. Pure composition bounds, repeat-cell enumeration, per-element
culling, renderer resource binding, reference scans, and route diagnostics now
share that contract.

The Theme Editor exposes complete-theme, selected-layer, and selected-element
previews; Add, Append Right, Duplicate, Delete, and back/front ordering;
searchable texture assignment; numeric position/scale; adjacent-edge snapping;
no-jump viewport dragging; finite and repeated modes; Fit Period to Content;
and visible repeat-cell guides. Diagnostics retain selected-texture edge facts
but label them as source facts, and separately report adjacent element bounds,
first/last wrap deltas, period-versus-content gap/overlap, and route coverage.
The former three-copy selected-texture preview was removed because it depicted
the wrong repeated unit once layers became compositions.

The cave gate established the missing abstraction: repetition is appropriate
for low-salience Far Fill and Far Formations, while repeating one distinctive
Near Formation plate exposes its landmarks. A second zone is not the solution.
Zones continue selecting complete environment themes for world regions;
stitching several pieces at one depth belongs inside one theme layer.

This milestone deliberately landed before the artwork pipeline. Reversing that
order would have generated assets against the known one-texture layer
limitation and forced immediate changes to Milestone 2 editor integration,
diagnostics, reference scans, and first production content.

#### Target data contract

Replace the single `texture_id` and `base_scale` on `ParallaxLayer` with an
ordered element collection:

```text
ParallaxLayer
  name
  scroll_factor
  offset                         # camera-relative layer anchor
  repeat_period                  # zero on an axis means finite on that axis
  elements[]                     # authored back-to-front within this depth

ParallaxElement
  stable integer id              # unique within the layer
  name
  texture_id
  position                       # layer-local world units
  scale                          # positive, finite
```

Repetition copies the complete element composition by `repeat_period`; it does
not independently tile each element. A one-element repeating layer preserves
the current Far workflow. A finite element strip supports unique Near scenery.
A longer `A → B → C → D` composition may also repeat as one deliberately
authored super-cell. Flip, tint, opacity, random choice, and animation remain
deferred until real content requires them.

The theme owns this composition. A zone continues to own only its world bounds,
theme reference, and future transition data. Do not add element placement to
`Level`, duplicate theme state per zone, or use adjacent zones to concatenate
pieces. Terrain-aligned scenery remains a Back Decor world prop rather than a
parallax element.

#### Implementation sequence

1. **Schema and intrinsic validation.** Add `ParallaxElement`, explicit repeat
   periods, stable element IDs, strict finite/positive geometry checks, at
   least one element per saved layer, and catalog validation of every element
   texture reference.
2. **Deterministic migration.** Preflight every theme and referenced texture.
   Convert each old layer to one element at `(0, 0)` with its old scale. For an
   enabled repeat axis, derive the period from the resolved native texture size
   times scale; use zero for a disabled axis. Refuse missing metadata,
   conflicting output, partially migrated documents, or a second run whose
   expected output differs. Load every shipped definition after migration and
   prove the rendered geometry is unchanged.
3. **Pure composition layout.** Replace the single-texture layout result with
   visible element instances identified by stable element ID and repeat-cell
   coordinates. Calculate the layer transform once, enumerate only repeat
   cells intersecting the camera, cull elements outside the view, and fail on
   invalid geometry or an excessive instance count.
4. **Resource and rendering boundary.** Resolve each unique element texture
   once per frame into copied immutable render input. Keep native handles in
   the renderer, emit elements in authored order, and preserve theme
   far-to-near ordering before all world layers.
5. **Theme Editor composition workflow.** Keep the far-to-near layer rail and
   add an element list for the selected layer. Support Add, Duplicate, Delete,
   reorder, searchable texture selection, numeric position/scale editing,
   `Append Right`, edge snapping, and no-jump viewport dragging. Make Complete
   Theme, Selected Layer, and Selected Element preview states explicit.
6. **Repeat and coverage authoring.** Expose Finite, Repeat X, Repeat Y, and
   Repeat X/Y through explicit period fields and a `Fit Period to Content`
   action. Draw the repeat-cell boundary and neighbouring cells in the preview;
   do not silently infer a new period after an author edits the composition.
7. **Diagnostics.** Retain per-texture edge facts, add adjacent-element seam
   review, first/last wrap-seam review for repeated compositions, overlap/gap
   warnings, and complete route coverage at supported zooms. Measurements aid
   review and do not claim artistic seamlessness.
8. **Lifecycle and cross-editor reconciliation.** Update texture deletion
   preflight and recipe deletion scans to visit every element. Save, duplicate,
   catalog refresh, reorder, deletion, and cross-editor navigation reconcile
   by stable IDs without retaining manager-owned pointers.
9. **Focused validation.** Add platform-neutral serializer/migration/layout,
   culling, repeat-cell, ordering, coverage, reference-scan, and editor-model
   tests. Keep ImGui tests to event wiring; do not add coordinate or screenshot
   tests. Run shipped-asset loading and the affected Level/Theme viewport
   targets before handoff.

#### Human workflow and gate — accepted

1. Preserve Far Fill as a one-element repeating composition and verify that
   migration produces the same output at zoom 0.5, 1.0, and 2.0.
2. Preserve Far Formations as a repeating composition, then add a second
   formation element and verify the complete group—not each texture—repeats.
3. Generate at least three Near Formation variants on an opaque `#ff00ff`
   background with visible content clear of every edge. Run each raw source
   through `process_generated_artwork`, using the accepted cave texture as its
   palette reference, and import only the reviewed transparent result. Append
   and overlap the variants into one finite strip, use numeric and direct
   manipulation controls, and inspect every adjacent seam. While adding each
   element, confirm its incomplete draft does not hide the existing backdrop;
   left-drag it after assigning a texture, then move the camera with both the
   arrow controls and Travel X/Y scrubbers. The exact processing command and
   source/output ownership rules live in `scripts/README.md`.
4. Preview the saved Cave level route with the Near strip finite. If the route
   is longer than its coverage, either add deliberate elements or author a
   longer repeated super-cell; do not add zones merely to hide repetition.
5. Save, close, reopen, reorder elements, and confirm stable selection,
   identical composition, coverage results, and Level viewport output.

The accepted gate used the saved `Cave Theme` and `Cave` level. The Cave world
is `65536×1280`; its intentional environment-zone route remains
`65536×1024`, which leaves enough world height for the 960×540 logical view at
0.5 zoom. Far Fill and Far Formations retain their migrated one-element
960×540 repeat cells. Near Formations is an accepted four-element horizontal
super-cell with a 5000-pixel repeat period and no vertical repetition. Its
element canvases span 5264 pixels, so neighbouring cells overlap by 264 pixels;
the first/last wrap and complete route were visually accepted. This repeated
multi-element Near composition exercises the same group-repeat contract as a
temporary second Far element, so duplicating that test was unnecessary.

The human review also accepted incomplete-draft preview behavior, direct
element selection and dragging, on-screen and keyboard camera movement,
Travel X/Y synchronization, save, close, reopen, reorder, and stable-ID
persistence. Automated shipped-asset, theme-manager, reference, migration,
layout, interaction, and preview-model checks remain the non-visual evidence.

Acceptance achieved: all old themes migrate without visual change; a theme
layer can compose and cull multiple texture elements; repetition copies an authored
composition with an explicit period; the cave Near layer can traverse its
reviewed route without an obvious one-screen motif repeat; and no level, zone,
recipe, or native-resource ownership boundary is weakened.

### Milestone 2 — import-first background artwork pipeline

**Status: implemented and accepted.**

- Generalize `SourceArtwork` storage and limits with a strict migration.
- Add `ParallaxArtworkRecipe`, serializer, manager, reference scans, and bundle
  lifecycle.
- Integrate the reusable `generated_artwork_postprocessor` stages and typed
  diagnostics. The command-line tool already proves chroma matting,
  premultiplied resizing, reference-palette quantization, binary alpha, and
  transparent-border validation; do not duplicate those stages in the editor.
- Add the Parallax Artwork editor with imported-source review, processing,
  wrapped repetition preview, create, regenerate, and delete.

Acceptance: an imported plate becomes a reproducible managed Texture, and
regeneration from retained source is pixel-identical.

#### Completion record

1. **Implemented.** Generalize retained source-artwork storage and its
   serialized limits without weakening existing Prop Artwork ownership;
   migrate and load every shipped definition.
2. **Implemented.** Add the versioned `ParallaxArtworkRecipe`, manager, output bundle,
   provenance, reference scans, and compensated create/regenerate/delete
   lifecycle.
3. **Implemented.** Call the existing `generated_artwork_postprocessor` from the shared
   deterministic operation. The CLI remains a thin adapter; the editor must
   not acquire a second matting, resizing, or palette-quantization path.
4. **Implemented.** Build the import-first Parallax Artwork editor around
   retained-source review, processing diagnostics, output preview, and
   lifecycle actions. Remote generation remains out until Milestone 3.
5. **Accepted human gate.** Import a raw cave plate, create and assign its
   managed Texture, regenerate pixel-identically after reopen, and prove
   deletion/reference refusal leaves no partial assets.

#### Human workflow and gate — accepted

The accepted gate imported
`cave-near-formation-pilot-magenta-source.png` as retained source
`86d5d138-e64b-4edc-8682-88d64bc416a6`, then created recipe
`fa03f699-3884-4976-8902-3a4cd0adfe8b` and managed Texture
`045787f7-81b8-485d-8d8a-dd37c4e7bd0e`. The saved recipe produces a 960×540
transparent overlay with fit-inside framing, border-connected solid-matte
removal, binary alpha at 128, the resolved cave palette, and both repetition
reviews enabled.

The human review accepted source import, processing-stage and repetition
previews, creation, close/reopen through the existing-recipe selector, complete
settings restoration, and pixel-identical regeneration. The managed Texture
was assigned to the saved Cave Theme, where it now appears at both ends of the
five-element Near composition with an 8192-pixel X repeat period. Referenced
texture and source deletion were refused without removing or partially changing
the recipe bundle. The architecture gate is accepted; final composition polish
and replacement of experimental high-colour candidates remain Milestone 4
content work.

#### Carried debt

- `cave-near-formation-pilot-v2.png` proves the magenta-background and exact
  cave-palette path. The Slant and Floor Ridge textures used in the accepted
  editor gate are older 1672×941 binary-alpha candidates with tens of thousands
  of colors, not normalized cave-palette outputs. They are test content, not
  final cave art.
- The remaining pre-pilot Near candidates are catalogued experiments. Reprocess
  or regenerate them through the shared pipeline before promoting them into
  the final kit; remove unused definitions and PNGs only through normal
  reference-checked asset lifecycle.
- The accepted 5000-pixel Near super-cell proves composition and avoids a
  one-screen repeat, but it is not a promise that its temporary element mix is
  the final Milestone 4 cave composition. Final seam, palette, silhouette, and
  gameplay-readability review belongs to that content milestone.

### Milestone 3 — generated candidates

- **Implemented.** Extract the shared image-generation request controller from
  Prop Artwork, including targeted request-ID collection, candidate navigation,
  retry-safe acceptance, and discard state.
- **Implemented.** Give Parallax Artwork independent background prompts and
  settings derived from layer role, target aspect, terrain style, transparency
  capability, and horizontal-repeat intent.
- **Implemented.** Wire existing Codex/OpenAI provider availability, generate,
  cancel, review, accept, and discard into the same retained-source path as
  import.
- **Implemented.** Keep at most one request per editor surface in flight and
  preserve the engine's existing global outstanding bound.

Acceptance: a live candidate can be accepted as retained source, processed,
committed as a recipe-owned Texture, and selected by a parallax layer; provider
failure leaves no partial assets.

Automated coverage proves the retained-source, processing, commit,
shared-engine routing, cancellation, and provider-failure boundaries. The
live-provider and parallax-layer selection portion remains the Milestone 3
human gate.

### Milestone 4 — theme workflow and first cave kit

- Produce the accepted Far Fill and Far Formations plus a small Near Formation
  element set.
- Create Back Decor, Gameplay, and Front Decor world layers in the cave level.
- Produce the minimum prop/decal kit and place each item according to its
  collision and visual-depth intent.
- Run the full camera route at minimum and maximum supported zoom, with layer
  visibility toggles used to inspect every depth plane.

Acceptance: the cave reads clearly with backgrounds, world-space back decor,
gameplay, and sparse foreground framing; removing any one depth band visibly
reduces depth without breaking gameplay readability.

### Milestone 5 — zone fades

- Implement the two-theme resolver and renderer opacity path.
- Validate supported adjacency and reject ambiguous multi-theme corners.
- Enable fade authoring controls and give the viewport a transition preview.
- Exercise two adjacent cave environment zones in both travel directions.

Acceptance: the camera crosses the shared edge without a hard cut, with the
same weights in editor preview and the future runtime composition boundary.

### Milestone 6 — operational cleanup

- Complete live image-provider smoke and shutdown checks shared with Prop
  Artwork.
- Add actionable provider and processing failures, retry only where existing
  generation policy proves it safe, and clean abandoned Zebes-owned staging.
- Reassess tint, opacity, wall attachment, animation, and foreground parallax
  against actual first-level pain rather than adding them by anticipation.

## 11. Human authoring workflow

This is the expected end-to-end workflow once Milestones 1-4 are complete.

### A. Establish the gameplay plane

1. Create or open the level and set its tile size, world bounds, spawn, and
   target camera/zoom range.
2. Rename `Base` to `Gameplay`.
3. Add `Back Decor`, move it behind Gameplay, then add `Front Decor` in front.
4. Lock Front Decor and Back Decor while painting collision terrain.
5. Select the cave tileset/terrain recipe and block out the complete playable
   route before decorating it.

### B. Build parallax textures

For each required output—one Far Fill, one or more Far Formations, and at least
three Near Formation variants:

1. Open Parallax Artwork and select the existing cave terrain recipe as the
   style source.
2. Choose Imported or Generate.
3. For generation, describe only this layer's role; do not ask for a finished
   composite scene.
4. Review the raw candidate, discard or accept it, then choose target
   dimensions, alpha role, pixel profile, palette policy, and repeat axes.
5. Inspect the native image and wrapped repetition preview at nearest-neighbor
   zoom. If a seam is visible, correct the source or choose non-repeating; do
   not accept a heuristic label in place of looking.
6. Create the recipe-owned Texture and record any warning that is intentionally
   accepted.

### C. Assemble the standalone parallax theme

1. In Theme Editor, create and save one Cave theme resource.
2. Add layers in far-to-near order. Add the Far outputs as one-element layers
   and add the Near variants as ordered elements of the Near layer.
3. Apply Far, Middle, and Near-background presets, then tune the layer anchor,
   element position, and element scale in their owning inspectors.
4. Enable repetition only on axes reviewed in Parallax Artwork. For a
   multi-element layer, author and inspect the complete repeat period.
5. Preview each selected element and layer alone, then the complete theme.
6. Select a level/zone camera context in the center logical game viewport.
   Scrub the camera through the complete route at supported zoom extremes and
   resolve coverage warnings in the bottom Diagnostics drawer. Use the manual
   route only when the theme is not yet assigned; its endpoints are camera
   centers, not world-content corners.
7. Save the theme explicitly.
8. In Level Editor, add a zone covering the intended route and assign the Cave
   theme from the resource catalog. Keep fade at zero until fade rendering is
   implemented.
9. Use Duplicate and Assign before changing the theme when the change should
   apply only to this level; otherwise edit the shared resource intentionally.

### D. Create and place props

1. Create/import props through Prop Artwork. Use grounded, ceiling, or free
   attachment according to the prop itself; visual depth is chosen later.
2. Give decorative background and foreground props no collider. A crystal or
   stalagmite that blocks or hurts the player is a distinct gameplay asset with
   an explicit collider or terrain shape.
3. Activate Back Decor and place muted wall crystals, distant stalagmites,
   columns, cracks, and entrances. Lock it when complete.
4. Activate Gameplay and place hazards, pickups, solid obstacles, and actors.
5. Activate Front Decor and place only large close silhouettes that frame the
   view without hiding landing surfaces, hazards, or the player for long.
6. If one formation must visually surround the player, author separate back
   and front pieces and place them in their respective layers.

### E. Validate and save

1. Run intrinsic level validation and catalog-reference validation.
2. Resolve every hard error; review each measured warning explicitly.
3. Toggle each world and parallax layer alone to find accidental ownership or
   ordering mistakes.
4. Enable collision visualization and verify that decorative art has none and
   gameplay obstacles have exactly the intended shapes.
5. Traverse the camera route at min/max zoom, inspect repetition seams, zone
   coverage, player silhouette, and foreground occlusion.
6. Save, close, reopen, and repeat the route. The reopened result must match.

## 12. Verification plan

Focused coverage should be added at platform-neutral boundaries:

| Boundary | Required coverage |
|---|---|
| `ValidateParallaxTheme` | required identity/content, stable element IDs, finite layer/element geometry, positive scale, and nonnegative repeat periods |
| `ValidateLevel` | theme-ID syntax, fade bounds, existing zone and world-layer invariants |
| level authoring readiness | categorized save, placement, and parallax prerequisites with actionable reasons |
| zone creation model | defaults, theme/bounds validation, cancel-without-mutation, commit, stable unique IDs |
| theme serializer/manager | strict fields, deterministic migration IDs, conflicts, load order, references |
| `ParallaxThemeEditorModel` | drafts, save, duplicate, layer/element add/reorder/delete, presets, stable-ID selection reconciliation |
| editor navigation | open-by-ID, duplicate-and-assign ordering, failure leaves level draft unchanged |
| background pipeline | framing, nearest-neighbor raster, palette mapping, alpha roles, wrapped preview, digests |
| recipe serializer/manager | strict required fields, unknown versions, duplicate IDs/names, round trip |
| source migration | exact rewrite/move, idempotence, conflict and half-migration refusal |
| API bundle lifecycle | preflight-before-write, compensation, regeneration snapshots, deletion references |
| generation controller | provider choice, cancellation, stale events, accept/discard ownership |
| viewport composition | authored layer/element order, group repetition, culling, texture resolution, coverage warnings |
| zone blend resolver | both directions, unequal widths, boundaries, overlaps, unsupported corners |
| shipped assets | every definition and referenced PNG loads after migration |

During implementation, run the narrowest affected executable. Before each
milestone handoff, run the complete affected test executables and
`git diff --check`; format edited C++ and lint all edited translation units in
one invocation. Extracting themes changes a broadly consumed serialized Level
field and warrants `scripts/build_and_test.sh` before Milestone 0 handoff. The
later SourceArtwork schema/path migration and new recipe/catalog references are
also cross-cutting enough to require the comprehensive check before their
final handoff.

## 13. Deliberately deferred

- Runtime level loading, game-loop, physics, player controller, and enemy
  behavior are separate prerequisites for a *playable* first level, not part
  of this artwork plan.
- Foreground parallax, per-layer blend modes, tint, opacity, and autoscroll.
- Automatic colliders or automatic background/foreground layer assignment.
- Wall-specific prop attachment; free placement remains the initial workflow.
- Generated animation sets and multi-part prop bundles.
- Automatic AI decomposition of one composite scene into coherent depth
  planes.
- A generic image workflow graph or plugin-discovered stage system.

Each may be revisited when first-level authoring produces a concrete failure
that the existing types cannot represent.
