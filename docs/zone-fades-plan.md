# Milestone 5 zone fades

**Status: Phases 1-4 are implemented and accepted. The complete affected
automated suite and the live two-zone visual gate pass.**

Implementation plan for the first two-theme parallax transition. Extends
[`environment-artwork-plan.md`](environment-artwork-plan.md) section 9 and
Milestone 5; it does not change the persisted level format.

The implementation started from a narrower point than the older roadmap text
implied:
`ParallaxZone::fade_length` is already authored, serialized, and intrinsically
validated for finite, non-negative values no larger than half the zone on the
corresponding axis. The Level Editor deliberately disables those fields,
`ResolveActiveParallaxZone` returned one zone, and `ParallaxRenderBatch` had no
opacity. Phases 1-3 implement the missing cross-zone geometry, composition,
and authoring path; Phase 4 records the accepted live gate.

## Design decisions

Settled for Milestone 5 -- raise a flag before deviating.

**D1 -- Zone transition semantics belong to the level domain.** Move the
active-zone result and resolver out of `editor/level_editor/parallax_layout`
and into `objects/level`. The resolver consumes only `ParallaxZone`, authored
order, and a world-space reference point; `ValidateLevel`, the editor, a
headless test, and a future runtime must use the same geometry. Parallax image
layout remains in `parallax_layout`, while SDL/ImGui opacity remains in
`ViewportRenderer`.

**D2 -- One exact shared edge defines one linear blend span.** Two rectangles
are adjacent only when their authored coordinates share an exact vertical or
horizontal edge and their projection onto that edge has positive length. Do
not add an epsilon that silently turns a gap or overlap into adjacency.

For a vertical edge, order the zones left then right; for a horizontal edge,
order them top then bottom. Those are the stable primary and secondary themes
for the complete transition, independent of which half-open zone contains the
camera center. If the primary inward width is `a`, the secondary inward width
is `b`, and the shared edge is at `e`, the transition interval is
`[e - a, e + b]` and:

```text
secondary_weight = clamp((coordinate - (e - a)) / (a + b), 0, 1)
```

The same formula applies on Y for a horizontal edge. It makes unequal widths
and a fade authored on only one side continuous. Both widths zero preserve the
current hard cut: the seam builder emits no seam when `a + b == 0`, so the
division above never sees a zero denominator. Pin that with a test rather than
relying on the clamp. A zero-weight seam pair remains in the domain result so
the boundary is observable, but viewport composition omits its invisible
secondary batch. Exact one endpoints and two zones referencing the same theme
canonicalize to a one-theme result.

Continuity holds only along the travel axis of the seam. At the end of a
partial shared edge, moving parallel to the edge inside the band snaps the
blend from up to `a / (a + b)` to nothing when the camera leaves the shared
projection. This pop is accepted Milestone 5 behavior — the alternative is the
corner compositor excluded by the non-goals — and is not a defect for the
human gate to flag when it appears in real content.

The resolved value carries the half-open active zone ID separately from the
stable primary/secondary zone and theme IDs. This preserves status, selection,
and overlap semantics without changing render order at the shared edge.

**D3 -- Later authored overlap priority remains absolute.** First resolve the
active zone by the existing reverse authored-order containment test. A fade
seam may apply only when that active zone is one of the seam's two zones. An
area-overlapping later zone therefore suppresses a seam rather than becoming a
third blend input. Missing neighbors are valid and leave the active theme
unblended; `fade_x` and `fade_y` describe capabilities at matching shared
edges, not required references to neighbors.

**D4 -- The first compositor accepts one seam at a point.** Build fade bands
from all non-zero shared-edge transitions during validation. Reject:

- positive-area intersections between different fade bands, including
  perpendicular bands at T and four-way corners;
- a fade band that passes through the positive-area overlap of a third zone;
- any runtime point for which more than one seam nevertheless resolves.

This is intentionally a one-seam rule, even if two ambiguous seams happen to
reference the same pair of theme IDs: they still produce two different
weights. Bands that only touch at an edge or point are not an intersection.
Errors must name the involved zones and the unsupported geometry so Level
Editor readiness is actionable.

Touching bands make closed membership ambiguous: when one zone's edge is
shared partially by two neighbors, the two bands touch along a line, a camera
center exactly on that line lies in both closed bands, and D3 does not
disambiguate because the shared zone belongs to both seams. Band membership is
therefore half-open along the edge axis, consistent with the zone activation
rule, so exactly one seam contains any point of validated geometry. The
defensive multiple-seam resolver error remains, but it guards against
geometry validation failed to reject — its test must construct that state
directly, not reachable authored input.

**D5 -- Blend opacity is platform-neutral; native tint is not.** Add a finite
`[0, 1]` opacity to `ParallaxRenderBatch`/composition options. Viewport scene
code produces the values, and `ViewportRenderer` converts them to the native
image tint passed to ImGui. The primary batch is the stable base; the secondary
batch uses `secondary_weight` as its overlay opacity. This avoids exposing SDL
or ImGui types outside the adapter and avoids the canvas showing through during
the middle of a transition.

The first implementation uses the current theme draw path rather than an
offscreen theme framebuffer. The human gate must look specifically for
layer-by-layer alpha artifacts. If the accepted cave themes do not crossfade
cleanly through native image tint, stop and add a whole-theme composition
target; do not compensate with hand-tuned weights.

## Phase 1 -- Domain resolver and strict geometry (implemented)

After this phase transition weights are fully determined and testable without
the editor or a rendering backend.

- Replace `ActiveParallaxZone` with a platform-neutral resolved-environment
  value in `src/objects/level.{h,cc}`. It contains the active zone ID, primary
  zone/theme, optional secondary zone/theme, and normalized secondary weight.
- Move `FindParallaxZoneById` and the half-open activation rule out of
  `parallax_layout`; keep parallax element layout concerns there.
- Build deterministic shared-edge seam descriptions from authored zone order.
  Use the same seam builder for resolution and cross-zone validation so the
  two paths cannot disagree about adjacency or fade extents.
- Extend `ValidateLevel` with D4. Keep the existing per-zone finite, bounds,
  and half-dimension checks. No schema version or definition migration is
  needed.
- Make resolution return `StatusOr<optional<...>>`: no containing zone is a
  normal empty result, while ambiguous or invalid transition state is an
  error rather than an arbitrary winner.

Focused tests in `tests/objects/level_test.cc` (moving the old activation cases
out of `parallax_layout_test.cc`) pin:

- no containing zone, half-open bounds, and later overlap priority;
- vertical and horizontal weights on both sides of an edge;
- unequal widths and both forms of one-sided fade;
- two zero widths, missing neighbors, partial shared edges, and points beyond
  the shared projection;
- same-theme canonicalization and exact zero/one endpoints;
- two bands touching along a line, resolving to exactly one seam on the shared
  boundary via half-open band membership;
- intersecting fade bands, a band entering a third-zone overlap, and the
  defensive multiple-seam resolver error;
- validation messages that identify the zones responsible.

Acceptance: every weight and refusal is platform-neutral, and the current
zero-fade shipped levels resolve exactly as before.

## Phase 2 -- Opacity-aware viewport composition (implemented)

After this phase Active Zone preview renders one or two complete themes from
the Phase 1 result.

- Add batch opacity to `ParallaxRenderOptions` and `ParallaxRenderBatch` in
  `viewport_scene`; reject non-finite or out-of-range values before producing
  native draw work.
- In `ViewportRenderer::RenderParallax`, validate defensively and convert the
  batch opacity to the white ImGui tint with a rounded alpha byte. Do not
  mutate SDL texture alpha state; handles remain owned by the texture store.
- Split `ViewportTab::RenderParallaxBackground` into a composable preparation
  step and the thin render loop. Active Zone mode uses the resolved pair;
  Selected Zone remains an explicit unblended inspection of the selected
  theme; Off resolves the active ID for status/gizmos but performs no theme or
  texture lookup.
- Resolve the two immutable theme snapshots by stable ID, collect the union of
  their texture IDs, and call `Api::GetTextureHandle` once per unique texture
  for the frame. Missing themes, textures, or handles fail the frame with the
  existing strict behavior.
- Submit the primary batch first at full opacity and the optional secondary
  batch second at `secondary_weight`. Do not interleave layers from different
  themes or reorder within a theme.

Focused tests in `viewport_scene_test.cc` and `viewport_tab_test.cc` cover
opacity validation/preservation, one- and two-batch preparation, stable batch
order across the half-open edge, texture deduplication, Selected Zone
isolation, Off mode, and missing resources.

Acceptance: the platform-neutral render description contains the exact Phase
1 weight, and native rendering is the only place that knows the tint format.

## Phase 3 -- Authoring and observable preview (implemented)

After this phase an author can set, diagnose, preview, and save supported
fades without hidden behavior.

- Enable Fade X and Fade Y in `parallax_zone_panel.cc`. Describe them as inward
  distances used at matching shared edges; retain the explicit reset-to-zero
  action.
- Do not silently clamp a temporarily invalid edit. `ValidateLevel` remains
  authoritative and `LevelAuthoringReadiness` prevents Save while showing the
  actionable geometry error. Numeric controls may use practical pixel steps,
  but typed values remain exact authored input.
- In the viewport status bar, retain the half-open active zone name and, while
  blending, also show the primary-to-secondary zone names and rounded blend
  percentage. This makes asymmetric fades inspectable without adding a second
  debug compositor. With a one-sided fade where the primary width is zero, the
  readout at the shared edge correctly says the active zone is the secondary
  while the render is still 100% primary theme at 0% blend — that is D2's
  stable ordering, not a bug, and the readout test should pin it.
- Active Zone preview follows the camera center through the fade. Selected
  Zone continues to isolate one theme so selection does not masquerade as
  runtime activation. Existing zone outlines remain editor-only overlays.
- Replace the test that calls fades unsupported with panel tests for editable
  values/reset, and add readiness coverage for an unsupported corner.

Acceptance: an invalid corner cannot be saved, a valid fade persists, and the
viewport readout and visible blend change together as the camera crosses the
edge.

## Phase 4 -- Verification and live gate accepted

Automated verification, narrowed while each slice lands:

```bash
scripts/test.sh level_test
scripts/test.sh parallax_layout_test
scripts/test.sh viewport_scene_test
scripts/test.sh --ui viewport_tab_test
scripts/test.sh --ui parallax_zone_panel_test
scripts/test.sh level_authoring_readiness_test
```

Before handoff, run the complete affected executables, use
`scripts/test.sh --affected-target level` to bound the consumers of the domain
change, format every edited C++ file, lint all edited translation units in one
invocation, and run `git diff --check`. The change is not expected to require
the repository-wide build unless the affected-target result proves broader
than these boundaries.

Live acceptance uses two adjacent cave zones with visibly different themes and
asymmetric inward widths (for example 512 px and 128 px):

1. Cross the shared edge in Active Zone preview in both travel directions.
2. Verify the readout follows the same weights at the same camera centers in
   either direction, including the half-open edge.
3. Switch to Selected Zone and confirm it isolates the selected theme instead
   of blending.
4. Set both widths to zero and confirm the old hard cut returns.
5. Save, reopen, and repeat the route to prove persistence.
6. Inspect Far Fill and transparent formation layers for dimming, ghosting, or
   an order flip at the boundary. If any appears, treat it as a renderer defect
   under D5 rather than accepting content-specific tuning.

Record the accepted geometry, camera centers, weights, and visual result in the
Milestone 5 section of `environment-artwork-plan.md`, then mark the roadmap
milestone complete.

### Accepted live evidence

Accepted 2026-08-27 in the saved `Cave` level:

- `Webbed Gallery` (zone `0`) and `Ossuary Descent` (zone `1`) share the exact
  vertical edge at world X `4096` over Y `[0, 1024)`;
- the primary inward width is `512` pixels and the secondary inward width is
  `128` pixels, so the transition interval is X `[3584, 4224]`;
- representative camera centers X `3584`, `3904`, `4096`, and `4224` resolve
  secondary weights `0%`, `50%`, `80%`, and `100%` respectively;
- Active Zone preview visibly blends at that boundary, and Selected Zone
  isolates each theme instead of blending it;
- the visual pass found no hard boundary, layer-order flip, or unacceptable
  alpha artifact, so D5 does not require a whole-theme framebuffer.

The saved level retains the accepted asymmetric geometry. Automated coverage
pins both travel directions, the half-open edge, zero-width hard cuts,
serialization, unsupported corners, and the exact resolver/renderer weights.

## Non-goals

- No schema migration and no replacement of the existing symmetric
  `fade_x`/`fade_y` fields with per-edge controls.
- No three- or four-theme corner compositor, fuzzy adjacency, polygonal zones,
  spatial index, easing curves, or content-specific fade weights.
- No per-layer tint, opacity, blend mode, autoscroll, or foreground parallax.
- No new runtime renderer. The shared domain result is the future runtime
  composition boundary; the current deliverable is its tested editor adapter.
- No cave-kit palette or silhouette curation in this milestone.
