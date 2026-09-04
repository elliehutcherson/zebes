# Mouse player production asset findings

Implementation completed 2026-09-03 on `animation-recipe-lifecycle` at
`13b4542`. This record preserves the asset and pipeline evidence. Active
sequencing remains in [`../handoff.md`](../handoff.md) and
[`../roadmap.md`](../roadmap.md).

## Outcome

The mouse is now a persisted production player asset rather than a reusable
proxy. One authored Blender model produces the complete left/right idle, run,
and airborne set. The existing player Blueprint identity and 32×64 collider are
unchanged; Catacombs entity 4 resolves the production idle-right Sprite.

The asset implementation is complete. The remaining gate is a recorded human
review of live input transitions. Runtime boot and focused in-level rendering
pass, but macOS denied Terminal screen capture because Screen & System Audio
Recording permission is not granted.

## Authored source

`experiments/character_binding/render_mouse_production.py` renders deterministic
48×48 RGBA frames with transparent borders:

| Clip | Frames | Timing | Playback |
|---|---:|---:|---|
| idle-right | 4 | 15 ticks/frame | loop |
| idle-left | 4 | 15 ticks/frame | loop |
| run-right | 8 | 4 ticks/frame | loop |
| run-left | 8 | 4 ticks/frame | loop |
| airborne-right | 4 | 4 ticks/frame | hold-last |
| airborne-left | 4 | 4 ticks/frame | hold-last |

The finished model adds the identity details absent from the proxy: eye patch
and glint, cheek and muzzle planes, ear highlights, hood, scarf, asymmetric coat
panels, lapels, pockets, cuffs, larger hands, highlighted boots, and a posed
tail. Grounded frames meet the authored contact band. Airborne frames retain a
visible ground gap and at least one transparent border pixel.

The renderer writes individual frames, six source strips, a complete contact
matrix, a render manifest, and the import manifest consumed by
`scripts/import_animation_frame_sets.cc`.

## Persisted asset graph

Stable player identities:

- Blueprint: `1be81945-b011-4342-9109-a10c4040078c`
- Collider: `2ef185c9-3a26-4729-8373-416e36ed67c7`
- Catacombs player entity: `4`

| State | Sprite | Recipe | Retained source |
|---|---|---|---|
| idle-right | `9cdadc72-2196-4820-b62a-29d133ebb30a` | `280929d1-3a5c-4e23-afe0-d6423b154bc1` | `8e320873-305f-4e47-9c54-bdd0a769e9a4` |
| idle-left | `47bac0c0-9d23-42a8-8aab-c8744cf9c98b` | `4048d5a2-5f13-45ec-a779-230abd07cfa7` | `90443b98-9ded-46a5-8bce-42b7641ef8c3` |
| run-right | `8c8eabf3-604b-46fe-876f-d8e28f6fa909` | `8ed8e459-62f4-4483-8ddf-aa3beadc10cf` | `fc2cd486-caec-4b02-85a4-c7601e859dc1` |
| run-left | `76afab54-06b1-47b8-a879-64af36366aff` | `2a4912a6-cd16-49eb-8091-5def6da94f8d` | `c7962278-233e-4325-9af2-54d10b6c7c1a` |
| airborne-right | `de2f1e3b-276e-437c-9386-0129ad682f20` | `0ac7d4d1-2ab9-4663-a18a-b9c3010eb7bd` | `bf6fd01e-1061-4293-ac02-7fcd5a50bed7` |
| airborne-left | `aa03d095-d3cf-4113-aa55-584006537d03` | `a167f00f-4a16-4842-b64a-17620aae6deb` | `31e20ca3-2cbd-4cba-ab7b-f61aa8dcec2a` |

Each recipe owns its processed Texture, Sprite metadata, timing, playback mode,
source digest, and prior Blueprint binding. The headless importer retains each
source as imported artwork, prepares through `AnimationFrameSetPipeline`, and
commits through the transactional API. If a later clip fails, completed clips
are deleted in reverse order; rollback continues after individual compensation
failures so every reachable cleanup is attempted.

The obsolete generated single-frame mouse placeholder graph and its 1.4 MiB
source image were removed.

## Pipeline findings

Production import exposed three real integration issues:

1. Initial feet extended one pixel below the contact line. Camera framing was
   corrected rather than weakening validation.
2. `ValidateAnimationFrameSetRecipe` multiplied native atlas dimensions by
   `render_scale`, contradicting `AnimationFrameSetPipeline`. Texture geometry
   now remains native while render dimensions and offsets scale. A regression
   test covers `render_scale = 2`.
3. Catacombs entity 4 retained the old idle Sprite snapshot after its Blueprint
   changed. The entity snapshot was migrated to the production idle-right
   Sprite, restoring runtime graph consistency.

`SpriteReviewer` now publishes the Milestone 5 evidence required for animation:

- ordered strip;
- native and nearest-neighbour enlarged frames;
- multi-frame contact sheet;
- red origin, cyan contact line, and yellow foreground-bounds overlay;
- every adjacent-frame difference with changed/union pixel counts;
- last-to-first closure for loops; and
- retained final-pose evidence for hold-last clips.

Two independently published persisted run-right review directories were
byte-for-byte identical.

## Visual findings

The complete 32-frame matrix preserves mouse identity, palette, clothing, and
facing across all clips. Run poses expose a readable alternating gait without
whole-character registration oscillation. Airborne frames keep the same model
and clear the ground.

Focused Catacombs review passed at 0.5×, 1×, and 2×. At 2× the ears, eye patch,
scarf, coat, hands, boots, and silhouette remain distinct against the route.
`run_game --asset_root=assets` loaded the production graph and ran for 36.7
seconds before a clean supervised stop.

## Verification

The complete affected set passed after formatting:

- `animation_frame_set_pipeline_test` — 9 tests
- `animation_frame_set_recipe_test` — 7 tests
- `animation_frame_set_asset_test` — 5 tests
- `animation_frame_set_recipe_manager_test` — 3 tests
- `animation_frame_set_api_test` — 19 tests
- `sprite_reviewer_test` — 3 tests
- `shipped_assets_test` — 19 tests
- `loaded_level_assets_unit_test` — 8 tests
- `runtime_world_test` — 15 tests
- `player_simulation_test` — 3 tests
- `scene_composition_test` — 8 tests

Total: 99 tests across 11 executables. Clang-tidy passed every supported edited
translation unit. The importer target compiled successfully, and the six-clip
transaction passed first in a disposable asset root and then against `assets/`.

## Decisions

- Blender remains an offline authoring adapter. No Python or Blender dependency
  enters the engine.
- The source is treated as imported/manual artwork. Provider-generated animation
  remains deprecated.
- No normal sidecar ships. Texture and Sprite definitions have no normal-map
  channel, so an unconsumed file would not be a production feature.
- The stable player Blueprint and collider IDs remain unchanged to preserve
  runtime ownership and level identity.

## Resume point

1. In macOS System Settings, grant Terminal access under **Privacy & Security →
   Screen & System Audio Recording**.
2. Launch `build/dev/bin/run_game --asset_root=assets`.
3. Visibly exercise and record idle, left/right direction changes, running,
   jump/fall, landing, slopes, walls, and ceilings in Catacombs.
4. If that human gate passes, mark production-player Milestone 7 complete and
   unblock runtime M4. Do not reopen image generation or remodel the accepted
   source unless the live review identifies a concrete visual defect.
