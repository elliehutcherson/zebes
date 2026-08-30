# Prop artwork pipeline

**Status:** Imported and generated prop authoring, deterministic processing,
strict source/recipe resources, transactional bundle lifecycle, editor review,
and headless generation are implemented. Remaining work is provider integration
and crash-recovery follow-up, not core prop processing.

The completed implementation narrative and measurements are preserved in
[`history/prop-artwork-plan-through-2026-08-30.md`](history/prop-artwork-plan-through-2026-08-30.md).

## Output contract

One prop recipe produces one retained-source bundle:

```text
SourceArtwork
    ↓ PropRecipe
Texture → Sprite → Blueprint state
```

The first packaging boundary is one Texture per prop. Shared atlases require
measured runtime need and a separate slot/compaction design.

A prop recipe owns artwork and frame geometry only. It does not infer collision,
world layer, sort order, or placement. Blueprint collider states and authored
level placements remain external.

## Ownership

- `SourceArtworkManager` owns lossless imported/generated source and provenance.
- `PropRecipeManager` owns strict versioned rebuild intent and output IDs.
- Texture, Sprite, and Blueprint managers own their normal definitions.
- `Api` coordinates graph-wide prepare/commit/regenerate/delete transactions.
- Editor models own drafts and pending-work state; panels report intents.
- Background/provider workers receive copied immutable inputs and return values
  or events. Manager and GPU mutation returns to the editor thread.

Callers keep stable IDs or copied snapshots, not manager pointers across saves or
catalogue refreshes.

## Style contract

The production palette is the complete resolved terrain palette snapshot, not a
small hand-picked subset. Recipes retain the exact style snapshot used to build
the output so later terrain edits do not silently restyle existing props.

An author may detach style ownership while preserving the snapshot. Depth and
layer assignment remain level-authoring decisions.

## Source and provider boundary

Imported PNGs and generated candidates enter the same bounded retained-source
path. `ImageGenerationSpec` is provider-neutral and owns prompt, optional
negative requirements, candidate count, target aspect, transparency preference,
and ordered copied image references.

OpenAI and Codex adapters validate capabilities, remain cancellable/non-blocking,
and return Zebes-owned images plus provenance. Credentials, HTTP, App Server
JSON, filesystem paths, and provider-native types do not cross the adapter.

Remote generation is optional source acquisition. A prop can always be imported
and processed without credentials.

## Deterministic processing

Processing is a typed platform-neutral C++ sequence over copied `RgbaImage`
values:

1. **Decode/validate:** positive dimensions, exact RGBA storage, byte/pixel
   limits, orientation, and nonempty content.
2. **Isolate:** prefer meaningful alpha; otherwise remove only border-connected
   declared/estimated matte.
3. **Compose/anchor:** fit the isolated subject into an authored tile-measured
   canvas and derive grounded, ceiling, or explicit free anchor geometry.
4. **Rasterize:** use premultiplied-alpha area filtering, then integer nearest
   expansion for authored pixel-block size.
5. **Quantize:** map opaque pixels to the retained palette snapshot with stable
   tie-breaking.
6. **Edge treatment:** optional inside-alpha outline using retained style roles.
7. **Cleanup/validate:** binary alpha, zero RGB under transparency, explicit
   component removal, bounds/contact checks, and byte-stable output.

Stages return diagnostics and never mutate managed assets. Preview runs the same
coordinator and differs only in which stage artifacts the editor presents.

## Attachment modes

- **Grounded:** anchor at the bottom contact surface.
- **Ceiling:** anchor at the top attachment surface.
- **Free:** explicit anchor inside the final canvas.

Attachment describes visual origin, not collision intent or world depth.
Changing attachment invalidates downstream preview and regeneration snapshots.

## Recipe and bundle

`PropRecipe` records source ID/digest, retained style snapshot, canvas,
attachment, every processing option, owned Texture/Sprite/Blueprint IDs,
expected frame geometry, output digest, and implementation version.

The initial one-frame Sprite uses the complete final image at 1:1 source size;
render offsets derive from the authored anchor. The Blueprint receives one
explicit state and no inferred collider.

## Transactions

Preparation returns final pixels, definitions, recipe data, diagnostics, and
optimistic snapshots without writing.

Creation preflights every ID/path/reference and publishes dependencies before
the recipe. Failure compensates in reverse order. Regeneration preserves IDs,
updates only recipe-owned artwork/frame geometry, and refuses stale source,
recipe, texture, or sprite snapshots. Deletion scans external references and
removes only the complete owned graph.

Generate-new-source and Save As are separate operations; neither silently
replaces retained input.

## Editor and headless flow

The Prop Artwork tab uses source/context controls, candidate/stage review, and
output/commit state. Edits invalidate prepared results. Review-each-step retains
source, isolation, composition, raster, quantization, edge, cleanup, and context
artifacts; finished-only mode exposes only final review.

`generate_assets` uses the same `AssetWorkspace`, provider service, retained
source, recipe template, and immutable candidate bundle. Accepted candidate
commit and deterministic re-review remain explicit commands.

## Remaining work

- Run the credential-gated OpenAI integration check.
- Complete the real Codex editor accept/discard/cancel/shutdown walk.
- Harden provider retry/shutdown behavior only from observed failures.
- Recover abandoned session-owned imports after process crash.
- Add Windows Codex process transport before claiming Windows support.

These do not block imported prop authoring or the current environment route.

## Non-goals

- Automatic world layer, sort order, or collision assignment.
- Generic workflow/plugin graphs.
- Shared prop atlas packing without profile evidence.
- Animation through repeated static-prop runs; production animation uses the
  imported/manual frame-set pipeline.
- Parallax backgrounds through prop recipes.

## Verification

Tests cover bounded ingestion, isolation, anchors, raster/palette/alpha rules,
recipe serialization, manager loading, reference scans, stale snapshots,
compensation, regeneration/deletion, editor model states, fake-provider
lifecycles, and headless candidate bundles. Human review decides native
readability, palette fit, anchor/contact quality, and in-level scale.
