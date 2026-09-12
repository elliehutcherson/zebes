# Mouse 3D authoring and run animation — plan of record

Updated 2026-09-12. **User-accepted success; this is the active plan of record.**

The user accepted the neutral storybook mouse and the 3D-to-sprite approach:

> You did it. This is clearly the way. This is fantastic.

They requested preservation in the repository and a running animation from this
model. This decision supersedes the earlier layered-2D, painted-part, atlas,
pose-image cleanup and procedural-mouse experiments as the main character route.
Their evidence remains useful history; their pending gates are not prerequisites.

## Accepted master

- **Authoring source:** [neutral-mouse.blend](../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend)
- **Portable export:** [neutral-mouse.glb](../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.glb)
- **Texture:** [study-basecolor.png](../experiments/mouse_3d/storybook-neutral-v1/result/study-basecolor.png)
- **Decision and hashes:** [acceptance.json](../experiments/mouse_3d/storybook-neutral-v1/acceptance.json)
- **Review:** [neutral study](../experiments/mouse_3d/storybook-neutral-v1/result/review.html)
- **Inputs, raw/clean meshes and exact source:** [asset record](../experiments/mouse_3d/storybook-neutral-v1/README.md)
- **Reproduction and tool details:** [implementation record](../experiments/storybook_mouse/README.md)

The Blender file is the authoritative starting point for animation. Its texture
is packed, it has no linked external model libraries, and its GLB embeds the
texture. The source, raw geometry, cleaned geometry, settings, scripts, tests
and accepted exports are retained with it. Downloaded model weights and remote
temporary directories are not required to open or animate the saved master.

Keep this accepted bundle unchanged. Start the run in a new versioned directory,
such as `experiments/mouse_3d/storybook-run-v1/`, by **loading the saved `.blend`
and saving a working copy**. Do not rerun imagegen, Hunyuan, mesh reconstruction,
decimation, automatic rebinding or texture baking merely to recover the model.
Do not substitute the older procedural v1/v2/v3 mouse.

## Established workflow

The successful route is a clean character reference → one reusable 3D mesh →
explicit cleanup, materials and rig → deterministic offline sprite rendering.
Blender owns the source model's geometry, posing, visibility, camera and lighting.
The game continues to consume ordinary validated 2D RGBA frames.

The accepted study contains 80,000 triangles, a 2048px baked color texture,
`Mouse_Neutral`, and the 20-bone `Mouse_Study_Rig`. The existing action is
`Neutral_and_two_body_checks`: frames 1/13/25 at 12fps represent neutral,
raised arms and a bent leg. These checks demonstrate a usable starting point;
they are not a run animation.

The model's visible appearance and the workflow are accepted. The temporary
body rig may need targeted work for running. Hands/digits, tufts and facial
controls are known refinement areas, **not blockers for trying the run**.
Preserve the accepted mesh, UVs, texture and neutral appearance. Change rig or
weights only where a concrete locomotion defect requires it, and retain the
before/after evidence. Do not reopen the neutral-art acceptance gate by default.

## Next milestone: running animation from the accepted model

Start this in a new conversation after this accepted checkpoint, following
`AGENTS.md`'s context-discipline rule. Run-animation work is requested; another
approval of the model or plan is unnecessary.

1. **Load and verify the master.** Check its hash against `acceptance.json`,
   duplicate it into the run experiment and inspect the existing armature/action.
   Establish model axes from the saved scene: Z is up and the character faces
   approximately -Y. Left/right bone names refer to its neutral X layout; derive
   near/far from the chosen camera. The earlier procedural mouse's X-forward
   motion code is not a drop-in action for this rig.
2. **Set a fixed game camera and paw-contact contract.** Use an orthographic
   profile or modest three-quarter view with the character facing screen-right.
   A camera toward negative X sees -Y motion toward screen-right. Prefer a level
   camera so a common ground height projects to one row. Record scale, origin
   and ground; keep them fixed for every frame. Inspect the real paw geometry
   and author sole/toe/heel contacts attached to the foot bones. A bone endpoint
   above the sole is not itself a ground-contact point.
3. **Author the main run poses in 3D.** Start with contact, compression, passing
   and flight/recovery keys. Use the actual bone lengths, opposing leg phases
   and counter-swinging arms. Add stable IK targets/poles if useful. Audit joint
   placement and the animal-like lower-leg/paw structure before relying on the
   temporary rig for a full gait. Fix the specific rig/weight issue if a pose
   cannot be represented; preserve the character's accepted neutral shape.
4. **Build a closed in-place cycle.** Add controlled body lean/bob and restrained
   tail motion. Use a declared virtual forward speed so stance paws travel
   backward consistently relative to the body and remain planted relative to
   the represented ground. Keep head identity stable. The closing key must
   match the opening pose; omit its duplicate from the exported frame sequence.
5. **Render a reviewable run.** Start with 24 samples of the cycle and include
   a 12-sample view of the same action. Choose and record a convincing cadence;
   neither sample count nor frame rate is an artistic acceptance rule. Retain
   512px masters plus 96/128px previews, transparent PNG frames, packed sheets,
   frame metadata and a review with play/pause/scrub, ground/contact overlays
   and a legs-only or rig view when useful. No per-frame generator calls or
   per-frame silhouette resizing/position fitting.
6. **Validate and obtain the run verdict.** Check bone lengths, connected chains,
   weights, stance motion, sole penetration, frame bounds, loop closure and
   decoded exports. Inspect the animation at its actual sprite sizes. Passing
   tests establishes implementation behavior; the user's visible run verdict
   determines whether the motion works.

## Later work

- Refine hands, fur tufts, facial topology and expression controls as needed.
- Add the green cowl and clothing/equipment as separate rig-bound pieces, after
  the underlying run works. Keep a complete underlying body.
- Set shipping resolution/cadence based on the accepted run, then integrate
  through the existing animation-frame-set import path and runtime bindings.
- Add other movement/action clips after the run establishes the authoring path.

The current run experiment does not require a new engine framework, live 3D
runtime, normal-map asset format or production import.

## Tool and verification notes

Derry has Blender 4.0.2 and the RTX 3090. Eevee works with `DISPLAY=:0`.
Its distribution Blender needs NumPy on the Python path for GLB export. Existing
dependencies and the isolated Hunyuan setup are documented in the implementation
record, but running animation from the packed `.blend` does not require Hunyuan.
Keep the working ComfyUI environment intact.

The accepted checkpoint passes:

```bash
build/tileset-venv/bin/python -m unittest tests.storybook_shape_test tests.storybook_neutral_asset_test
git diff --check
```

Use focused checks for new animation work. Read this plan and the short handoff;
consult archived attempts only when a specific failure calls for that evidence.
