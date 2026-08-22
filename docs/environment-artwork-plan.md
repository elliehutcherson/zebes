# Environment artwork and parallax plan

**Status: Milestone 0 implemented; Milestone 1 next.** Written 2026-08-22 for
the first cave vertical slice and revised the same day to make standalone
parallax-theme ownership the highest-priority implementation milestone. Update
milestone states here as work lands; use [`roadmap.md`](roadmap.md) only for the
higher-level sequence.

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
| Content | One repeated or non-repeated texture | Sparse tiles and placed entities |
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
Its output is a whole managed Texture selected by a `ParallaxLayer`.

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

One cave theme should begin with three textures stored far to near:

1. `Far Fill`: opaque, very slow, covering the complete camera view.
2. `Far Formations`: distant silhouettes, usually transparent over the fill.
3. `Near Background`: closer arches, columns, roots, or crystals, still behind
   every world layer.

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

- rename `PropSourceLimits` at the storage boundary to
  `SourceArtworkLimits`; prop configuration may continue embedding an instance;
- move the canonical image directory from `source_art/props/` to a neutral
  `source_art/artwork/` path;
- add a strict schema migration that moves each ID-backed PNG and rewrites its
  definition without overwriting an existing target;
- let prop and parallax recipes both reference the same source;
- delete a source only after reference scans find no recipe of either kind.

The migration must load every shipped definition and must be idempotent. A
half-migrated definition or conflicting source/target file fails rather than
guessing which image is authoritative.

### 4.2 One recipe produces one texture

Add a versioned `ParallaxArtworkRecipe` under
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
  overlay extraction: preserve alpha or remove a border-connected flat matte
  repeat diagnostics requested for X and/or Y

output authority
  texture_id
  expected width and height
  final pixel digest
```

Scroll factor, scale, offset, repeat flags, and layer order do **not** belong in
the recipe. They are placement/composition decisions owned by
`ParallaxLayer` inside the standalone theme. The artwork editor may suggest
theme-layer defaults after creation, but there is only one serialized authority
for each value.

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
   - an opaque generated overlay may remove a configured, border-connected
     flat matte before continuing, while retaining every non-matte component
     even when the artwork intentionally touches an edge;
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
- requested matte removal cannot identify and clear a valid border-connected
  matte under its stored configuration;
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

- Add remaining intrinsic geometry/fade validation and catalog
  texture-reference checks.
- Add layer reorder, selection reconciliation, presets, live texture discovery,
  thumbnails, correct offset ownership, repetition diagnostics, and camera
  coverage diagnostics in Theme Editor.
- Mark fades unsupported in Level Editor until Milestone 5.
- Assemble one temporary three-plane cave background through the existing
  Texture, Theme, and Level editors.
- Preview it behind existing cave terrain and the Cave Crystal prop.
- Record target viewport, zoom range, texture sizes, desired repetition, and
  observed seam/coverage problems before choosing processing defaults.

Acceptance: a human can import three hand-authored PNGs, assemble and reorder a
standalone theme, assign it to a zone, save, reopen, and get the identical
composition. One camera view also establishes that the proposed
far/middle/near division produces useful depth without foreground parallax.

### Milestone 2 — import-first background artwork pipeline

- Generalize `SourceArtwork` storage and limits with a strict migration.
- Add `ParallaxArtworkRecipe`, serializer, manager, reference scans, and bundle
  lifecycle.
- Implement the deterministic background stages and typed diagnostics.
- Add the Parallax Artwork editor with imported-source review, processing,
  wrapped repetition preview, create, regenerate, and delete.

Acceptance: an imported plate becomes a reproducible managed Texture, and
regeneration from retained source is pixel-identical.

### Milestone 3 — generated candidates

- Extract the shared image-generation request controller from Prop Artwork.
- Give Parallax Artwork independent background prompts and settings.
- Wire existing Codex/OpenAI provider availability, generate, cancel, review,
  accept, and discard into the same retained-source path as import.
- Keep at most one request per editor surface in flight and preserve the
  engine's existing global outstanding bound.

Acceptance: a live candidate can be accepted as retained source, processed,
committed as a recipe-owned Texture, and selected by a parallax layer; provider
failure leaves no partial assets.

### Milestone 4 — theme workflow and first cave kit

- Produce the accepted Far Fill, Far Formations, and Near Background textures.
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

For each Far Fill, Far Formations, and Near Background layer:

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
2. Add layers in far-to-near order and select the three processed textures.
3. Apply Far, Middle, and Near-background presets, then tune scale and offset
   in the layer inspector.
4. Enable repetition only on axes reviewed in Parallax Artwork.
5. Preview each selected layer alone, then the complete selected theme.
6. Scrub the camera through the complete route at supported zoom extremes and
   resolve coverage warnings.
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
| `ValidateParallaxTheme` | required identity/content, finite layer geometry, positive scale |
| `ValidateLevel` | theme-ID syntax, fade bounds, existing zone and world-layer invariants |
| theme serializer/manager | strict fields, deterministic migration IDs, conflicts, load order, references |
| `ParallaxThemeEditorModel` | drafts, save, duplicate, add/reorder/delete, presets, selection reconciliation |
| editor navigation | open-by-ID, duplicate-and-assign ordering, failure leaves level draft unchanged |
| background pipeline | framing, nearest-neighbor raster, palette mapping, alpha roles, wrapped preview, digests |
| recipe serializer/manager | strict required fields, unknown versions, duplicate IDs/names, round trip |
| source migration | exact rewrite/move, idempotence, conflict and half-migration refusal |
| API bundle lifecycle | preflight-before-write, compensation, regeneration snapshots, deletion references |
| generation controller | provider choice, cancellation, stale events, accept/discard ownership |
| viewport composition | authored parallax order, texture resolution, coverage warnings |
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
- Foreground parallax, per-layer blend modes, tint, opacity, autoscroll, and
  independent repeat periods.
- Automatic colliders or automatic background/foreground layer assignment.
- Wall-specific prop attachment; free placement remains the initial workflow.
- Generated animation sets and multi-part prop bundles.
- Automatic AI decomposition of one composite scene into coherent depth
  planes.
- A generic image workflow graph or plugin-discovered stage system.

Each may be revisited when first-level authoring produces a concrete failure
that the existing types cannot represent.
