# Mouse 3D authoring and run animation

Updated 2026-09-12. The neutral character and 3D-to-sprite workflow are accepted.
**Run 03 is not accepted as a run. Animation revisions are paused for cleanup.**

## Source and preservation

Use the [accepted Blender master](../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.blend)
and its [acceptance record and hashes](../experiments/mouse_3d/storybook-neutral-v1/acceptance.json).
It contains `Mouse_Neutral`, the 20-bone `Mouse_Study_Rig`, and a packed texture;
the [GLB](../experiments/mouse_3d/storybook-neutral-v1/result/neutral-mouse.glb)
embeds its texture. Opening or animating the saved model needs no Hunyuan
weights or remote generation workspace.

Preserve the accepted bundle, [foot study](../experiments/mouse_3d/storybook-foot-study-v1/README.md),
and all three [run bundles](../experiments/mouse_3d/README.md) byte for byte.
Load a saved scene, verify its recorded hashes, and save a new versioned working
copy. Do not regenerate, rebind, or rebake the accepted character to recover it.

Blender owns offline geometry, posing, camera, lighting, and visibility. Zebes
continues to consume validated 2D RGBA frames. Earlier layered-2D, painted-part,
atlas, pose-cleanup, and procedural-mouse gates are superseded.

<a id="immediate-next-step-foot-shape-and-articulation-study"></a>

## Findings and current verdict

| Study | Decision and useful finding |
|---|---|
| [Run 01](../experiments/mouse_3d/storybook-run-v1/README.md) | Okay starting point; flat-footed and too slow. |
| [Run 02](../experiments/mouse_3d/storybook-run-v2/README.md) | “This isn't great.” A rigid toe pivot passed contact checks without convincing anatomy or motion. |
| [Foot study 01](../experiments/mouse_3d/storybook-foot-study-v1/README.md) | “Okay, this is much better.” Local foot/shin reshaping and two toe bones supply independent articulation. Five poses are a study, not a looped run. |
| [Run 03](../experiments/mouse_3d/storybook-run-v3/README.md) | “This is not a running animation.” The first preview improved the motion but read as high stepping; the stride/lean/fist revision still failed the run verdict. |

Run 03 retains the study's 22-bone rig and adds local hock-weight repair and a
reversible closed-paw shape. Remaining deformation concerns include the strongest
knee/hock fold and pelvis/tail-root stretch. Hands have partly fused digits;
the fist is a silhouette adjustment, not an anatomical finger rig.

Contact, bone-length, intersection, loop, and export checks establish technical
behavior. The user's visible verdict establishes animation acceptance. The
generated reviews' pending-verdict labels predate the decisions above.

<a id="current-review-run-03-with-longer-stride-forward-lean-and-fists"></a>
<a id="immediate-next-step-run-03-with-active-arms"></a>

## Next run

When animation resumes, start from the saved revised-foot/run work in a new
copy. Preserve the accepted character and foot articulation. The next run needs:

- A much larger **visible leg spread**, not only greater declared travel.
- Much stronger forward lean than run 03's roughly 13.5 degrees.
- The back foot extending much farther behind the body in rearward push-off.
- Readable opposing arms, changing elbows, inward-facing fists, and hand paths
  clear of the torso and thighs at 96/128px.

Run 03 could become a walk with slower playback and relaxed arms; no walk has
been authored or accepted. Cadence alone does not turn a walk into a run.

The positively reviewed [raptor reference](../notes/digitigrade-reference-2026-09-12/README.md)
supplies cadence and foot articulation, not mouse proportions or a horizontal
torso. Its nominal cycle is 16/30 seconds; world-space travel speed was not
measured. [Additional run references](../notes/run-reference-2026-09-12/README.md)
record inspected examples. Keep downloaded sources and attribution with them.

Show a complete closed-loop preview early, including the full character,
fixed side view, actual sprite sizes, and attributed reference comparison.
Judge the visible stride and rearward extension before further packaging.

## Authoring and export contract

1. Verify saved-source hashes. Keep mesh/rig/weight fixes local to measured pose
   defects and retain before/after edit-region evidence.
2. Use the saved model's axes: Z up, approximately -Y forward. Fix orthographic
   camera, scale, origin, and ground for the entire cycle. Derive near/far from
   the camera, and measure actual toe/sole geometry rather than bone endpoints.
3. Author contact, compression, push-off, flight, and folded recovery with
   constant bone lengths, connected joints, opposing limbs, and body motion.
   Declared virtual travel must keep stance contacts planted in ground space.
4. Close every animated channel at one cycle boundary and omit the duplicate
   closing pose from exports.
5. Retain 512px transparent masters, 96/128px previews, 24/12-sample views,
   sheets, frame metadata, and playback/scrub/contact/rig diagnostics. Use one
   whole-frame reduction; no per-frame fitting or image generation.
6. Check saved-file reopening, preservation, weights, bone lengths, attachments,
   ground/contact errors, intersections, deformation, bounds, loop closure,
   and decoded frame/timing data. Inspect playback at actual sprite sizes.

[Live implementation and verification commands](../experiments/storybook_mouse/README.md)
own tool setup. Individual bundle records own exact measurements and reproduction.

## Later work

Refine hands, tufts, facial controls, and skinning as actual motion requires.
After the run works, add separate rig-bound clothing over the complete body,
choose shipping resolution/cadence, import through the existing frame-set
pipeline, and author other clips and runtime transitions.
