# Active handoff

Updated 2026-09-12. **The storybook 3D mouse is an accepted success.**
The user said, “You did it. This is clearly the way. This is fantastic,”
requested that this become the plan of record, and asked for a running
animation from this model.

## Plan of record and accepted master

Read [Mouse 3D authoring and run animation](mouse-3d-plan.md). It is the active
work list. The old layered-2D, painted-part and procedural-mouse plans are
superseded; do not resume their approval gates or regenerate this character.

- [Accepted Blender master](../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend)
- [Textured GLB](../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.glb)
- [Acceptance decision and exact hashes](../experiments/mouse_3d/storybook-neutral-v1/acceptance.json)
- [Neutral model review](../experiments/mouse_3d/storybook-neutral-v1/result/review.html)
- [Asset inputs, raw geometry, source snapshots and evidence](../experiments/mouse_3d/storybook-neutral-v1/README.md)
- [Implementation and reproduction](../experiments/storybook_mouse/README.md)
- [Success record](history/storybook-3d-success-2026-09-12.md)

The master contains `Mouse_Neutral`, 80,000 triangles, a packed 2048px color
texture and the 20-bone `Mouse_Study_Rig`. It has no linked external model
libraries; the GLB embeds its texture. The accepted Blender SHA-256 is:

```text
32f9bb3c460bc09086cac0afcfb29e26941e2bea5bb8e726459e6caa223672c9
```

All source/model files needed to continue are in this repository. Hunyuan
weights and the remote temporary generation workspace are unnecessary for
animating the saved model.

## Next conversation: make the run

This is an accepted milestone and a workstream change; follow `AGENTS.md` and
start a fresh conversation for animation. The user has requested the run.
Do not ask for another approval of the model or plan.

1. Verify the master hash, load the saved `.blend`, and save a working copy
   in a new run directory. Preserve the accepted bundle unchanged.
2. Inspect the rig and actual paw contacts. The character faces approximately
   -Y, Z is up; the older procedural mouse used different axes. Set one
   orthographic game camera, fixed scale/origin and common ground.
3. Author run keys and a closed cycle in 3D. Preserve bone lengths and limb
   ownership; use explicit stance contacts and recovery trajectories. Make
   only the rig/weight changes that actual motion defects require.
4. Render a 512px master sequence and 96/128px previews with full playback,
   pause/scrub and useful contact/rig diagnostics. Compare full sampling and
   a 12-sample version of the same action.
5. Validate geometry/contact/loop/export behavior and get the visible run
   verdict before production import.

The current action `Neutral_and_two_body_checks` has neutral/reach/step keys
at frames 1/13/25, 12fps. It is not a run. Hands, tufts, facial controls and
clothing remain refinement work, but they do not block trying locomotion.
Keep the accepted appearance; do not restart neutral-art exploration.

## Verification and tools

The ten focused input/artifact tests pass. They check input/raw hashes,
bounded watertight cleanup, nonempty packed texture, mesh/UV/skin data,
constant bone lengths, connected chains, fixed head/support paw, ground and
transparent sprite margins. The user's verdict establishes artistic acceptance.

```bash
build/tileset-venv/bin/python -m unittest tests.storybook_shape_test tests.storybook_neutral_asset_test
git diff --check
```

Derry has Blender 4.0.2 and the RTX 3090. Eevee works with `DISPLAY=:0`.
Its distribution Blender requires NumPy on its Python path for GLB export;
the implementation README records the existing runtime/dependency paths.
Use a new remote run directory and keep the working ComfyUI installation intact.

[Earlier handoff and failed-route context](history/character-art-handoff-before-3d-acceptance-2026-09-12.md)
are archived evidence, not a competing work list.
