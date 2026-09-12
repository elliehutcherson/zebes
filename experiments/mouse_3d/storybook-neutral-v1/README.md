# Storybook neutral study — first implementation checkpoint

**Accepted success, 2026-09-12.** The user said, “You did it. This is clearly
the way. This is fantastic,” and selected this model/workflow as the new plan
of record. [Decision and exact asset hashes](acceptance.json) ·
[Active 3D/run plan](../../../docs/mouse-3d-plan.md)

- [Interactive review](result/review.html)
- [Blender model and temporary body rig](result/neutral-mouse.blend)
- [Textured GLB with the pose checks](result/neutral-mouse.glb)
- [Reference / clay / color comparison](result/comparison.png)
- [Turntable](result/turntable.gif)
- [Three pose checks](result/pose-grid.png)
- [96px](result/sprites-96.png), [128px](result/sprites-128.png),
  [512px](result/sprites-512.png) pose sheets
- [Baked study texture](result/study-basecolor.png)
- [Implementation, reproduction and limitations](../../storybook_mouse/README.md)

This is a new generated and cleaned 3D body/head. The temporary rig supplies
neutral, raised-arm and bent-leg checks. It has no facial/finger controls or
final run cycle. Its appearance and the authoring route are accepted as the
working character baseline. The hands, tufts and facial topology have known
refinement opportunities; those do not block starting the run from a working
copy of this saved model. Preserve the accepted files unchanged.

The Blender texture is packed and there are no linked external model libraries.
The GLB embeds its texture. The saved master can be opened and animated without
the remote Hunyuan weights/cache or temporary generation directory.

## Retained evidence

- [Input prompt](input-prompt.txt), [raw image](input-raw.png): first built-in imagegen request. The
  checkerboard is painted into RGB and is rejected as a reconstruction background.
- [Background prompt](background-prompt.txt), [clean input](input-clean.png): background-only built-in edit.
- `prepared/`: the exact inspected Hunyuan foreground, conditioning image/mask
  and input hashes. Foreground removal is the generator's standard rembg step.
- `model.json`, `config.yaml`: exact checkpoint revision, configuration and hashes.
- `raw-shape/`: untouched raw geometry, successful receipt and exact runner.
- `cleanup/`: inspected one-component repair and measured cleanup report.
- `inspection-v1/`: rejected single-view projection and original clay inspection.
- `head-depth.json`, `head-depth.png`: measured face-surface diagnostic.
- `study-v1/`: first body binding with coarse vertex-color appearance.
- `rejected-empty-bake/`: failed black-texture result retained separately.
- `result/`: corrected UV bake, clay/color views, temporary rig, pose checks,
  exported assets and exact Blender source snapshots.

The earlier procedural mouse versions and the original storybook concept are
preserved elsewhere in this experiment. No production asset was replaced.
