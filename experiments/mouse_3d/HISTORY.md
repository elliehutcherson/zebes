# Procedural mouse proof-of-concept history

Historical record through 2026-09-12. The procedural review gates below are
superseded by the accepted storybook model. Use [README.md](README.md) for
the preserved bundles and the [3D mouse plan](../../docs/mouse-3d-plan.md)
for current work.

The 2026-09-12 user request changes the experiment to an actual reusable 3D
asset. The green hood, brown fur, large round ears, pale muzzle, red scarf,
belt, stout trousers, boots and tail refer to the unchanged
[source mouse](../character_binding/inputs/interactive-run-source-v1.png).
This is a newly authored interpretation, not the previous rejected 48px model,
a projected picture, or a claim to match the painted illustration.

**New art direction:** the user subsequently supplied three references and
selected image 2, a storybook adventurer. See the
[design/modeling direction](../../docs/mouse-storybook-direction.md) and
[first 2D concept sheet](storybook-direction-v1/concept-sheet.png). That
proposal starts a new character-mesh direction; the working 3D proof below
remains retained. No new 3D asset is claimed by the concept sheet.

**Accepted success:** the [neutral storybook model](storybook-neutral-v1/README.md)
contains the generated/cleaned 3D head and body, a temporary body rig, two posed
checks and sprite previews. The user accepted this model and workflow on
2026-09-12 and made it the [plan of record](../../docs/mouse-3d-plan.md).
Running animation from its saved Blender master is next. The v1/v2/v3
procedural work below remains earlier evidence, not the active character base.

## Earlier procedural iterations

**Earlier candidate: v3, face and fabric.** The user strongly preferred
hood-down v2 and requested a bit more width, a shorter nose, cuter eyes and
folds in the pants. V2 is the preferred basis; v3 awaits user review.

- [Interactive review](v3/review.html): synchronized v2/v3 comparison, original
  drawing, full renders, 96/128px playback, pause/scrub, 12/24-frame sampling,
  projected limb joints and turntable.
- [Editable model, rig, animation and render scene](v3/mouse.blend).
- [Animated portable mesh](v3/mouse.glb).
- [96px sheet](v3/sheet-96.png), [128px sheet](v3/sheet-128.png),
  [512px sheet](v3/sheet-512.png), [frame metadata](v3/sprites.json).
- [Sprite playback](v3/run-preview.gif), [smooth playback](v3/run-smooth.gif),
  [3D turntable](v3/turntable.gif), [before/after](v3/before-after.png),
  [face detail](v3/face-detail.png), [trouser detail](v3/pants-detail.png).
- [Render manifest and geometry checks](v3/manifest.json).

Relative to v2, the torso/coat gain 9.4% depth and 10.8% width. Shoulder
half-width changes from 0.57 to 0.635 and hip half-width from 0.34 to 0.375.
The lower hood follows that broadened body. Sagittal bone directions, limb
lengths, foot paths, camera, frame scale and animation timing are unchanged.

The muzzle compression is smooth and leaves the cranium unscaled. Its authored
tip moves from x=1.17 to about x=0.993; the muzzle is about 23% shorter measured
from x=0.40. All facial parts receive that same transform, keeping the nose,
mouth and whiskers aligned with the single skin. Cheek cross sections are
slightly fuller. The eye lenses are rounder with warm iris colors, large pupils,
softer upper/lower lids and two catchlights. Their color attributes survive GLB
export.

Each trouser leg is one dense, weighted surface. Irregular ridge/trough pairs
produce diagonal thigh folds, knee compression folds and gathers above the
boot cuff. These are localized mesh forms with restrained surface shading and
an outer seam; they are not stripes drawn on a smooth leg. The folds deform
with the existing armature and remain authored rather than simulated cloth.

The original [v1 review](v1/review.html) and [preferred v2 review](v2/review.html)
remain intact, with their builder/motion/review sources alongside the assets.

### Retained v2 changes

The coat and torso are 28% deeper along the running direction and 30% wider
across the body. Shoulder half-width changes from 0.43 to 0.57; hip half-width
changes from 0.27 to 0.34. Thigh and upper-sleeve surfaces are fuller. Those
are mesh/proportion changes, not image stretching. The same sagittal gait and
fixed limb lengths remain in use.

The hood is a lined, open cloth pouch resting across the shoulders and back.
`forms.py` defines one continuous skull-to-muzzle skin with integrated jaw and
eye recesses. Feathered fur/muzzle colors are vertex attributes on that skin.
Shallow almond eye lenses follow its surface; the ears are closed cupped forms
with thinner rims and blended inner coloring. Separate cheek/muzzle spheres,
cream eye plaques and stacks of ear discs are gone.

Serve the directory to use the review's joint-overlay metadata. All media also
open as ordinary local files. Sheets use six columns and four rows in frame
order, with a fixed origin, camera, canvas and character scale. There is no
per-frame bounding-box fitting. The Blender file opens on frame 4; play frames
1–24 at 24fps. Frame 25 is the exact closing pose and is included in GLB's
one-second animation.

## Method

`build_mouse.py` authors mesh surfaces, materials, skin weights, an armature,
camera and lights in Blender 4.0.2. Head, hood, ears, eyes, whiskers, coat,
continuous weighted sleeves/trousers, boot feet/shafts/soles, paws and tail are
real geometry. Near/far limbs have separate named bone chains. Rigid feet pivot
at their ankles; boot shafts follow the shins. One model supplies all views.

`motion.py` authors a new sagittal run with analytic two-bone leg solves,
constant thigh/shin lengths, opposite arm phases, flat support soles and
lifted recovery feet. It does **not** reproduce the previous twelve-pose
drawing. Support feet move backwards at constant speed during their stance
interval; the scene is an in-place loop. Coat bones and tail motion are authored
cyclic secondary motion, not cloth or fur simulation.

Eevee renders transparent 512×512 masters with soft light, surface shading,
ambient occlusion and restrained procedural material variation. The packaging
script reduces those exact renders using Lanczos, a shared 48-color palette
per resolution, alpha threshold 128 and a one-pixel dark outline. This makes
the sprite processing explicit and repeatable. Small sprites remain baked color
images; this experiment does not add normal maps or change engine formats.

The `.blend` is the appearance authority. GLB carries geometry, skinning,
animation, base material colors and blended face/ear/eye/trouser vertex colors; the
procedural cloth/leather detail is not baked into portable texture maps.
Blender also uses volume-preserving skinning,
which standard GLB viewers may approximate with linear skinning.

The user's Dead Cells comparison is apt as an authoring workflow: Thomas
Vasseur describes creating a model and skeleton, animating poses, and exporting
small PNG frames. His account also identifies reduced detail and pixel shimmer
as tradeoffs. This proof uses Blender/Eevee and explicit sprite postprocessing,
not their proprietary renderer. [Artist's account](https://www.gamedeveloper.com/production/art-design-deep-dive-using-a-3d-pipeline-for-2d-animation-in-i-dead-cells-i-).

## Reproduce

From the repository root, with Blender 4.0.2 and its standard glTF/NumPy
dependencies available:

```bash
blender --background --threads 4 --python-exit-code 1 \
  --python experiments/mouse_3d/build_mouse.py -- --out /tmp/mouse-3d-new
build/tileset-venv/bin/python scripts/review_mouse_3d.py /tmp/mouse-3d-new \
  --previous experiments/mouse_3d/v2
build/tileset-venv/bin/python -m unittest tests.mouse_3d_motion_test tests.mouse_3d_asset_test
git diff --check
```

Output directories must be new. `--preview` builds the complete rig and checks
all 24 poses but renders frames 1/8/16, front/back views and face/trouser
close-ups. Asset tests
exercise retained `v1/`, `v2/` and `v3/` fixtures; motion tests require neither
Blender nor a GPU. New full renders retain the exact builder, motion and form
sources beside the assets; the review generator retains its source too.
`--export-only` opens a retained `.blend` and replaces just its GLB and export
metadata. It was used to correct Blender's default nonzero animation time
origin; the PNG masters were unchanged.

On `derry`, the installed distribution build exposed only the CPU to Cycles.
Its Eevee renderer works with `DISPLAY=:0`. Its glTF exporter also lacked NumPy
on Blender's default Python path. The successful run used the existing
`/home/ellie/ComfyUI/venv/lib/python3.12/site-packages` as `PYTHONPATH` plus
Blender's `--python-use-system-env`. No packages, drivers or ComfyUI state were
changed. The isolated work directory is `/tmp/zebes-mouse-3d-20260912` and the
initial complete render is `release-v1/`, copied locally as `v1/`. The final
hood-down render is `hood-down-v2-final/`, copied locally as `v2/`. The face
and fabric refinement is `v3-face-and-fabric/`, copied locally as `v3/`.

## Verification and limits

All 22 focused tests and `git diff --check` pass. The suite covers all three
retained versions. The saved v1 `.blend` was
reopened for its final GLB export. Later revisions retain their authored scenes,
exact source snapshots and all animation/export evidence. The current comparison
synchronizes previous/new frames and includes sprite controls and separate face
and trouser close-ups.

- Platform-neutral tests check 1,000 samples for limb lengths and attachments,
  foot clearance, constant stance speed, loop closure and invalid targets.
- Blender verifies that every rest matrix matches the mesh's authored bone
  coordinates, posed limb joints match their controls, and actual evaluated
  sole vertices remain above ground. Planted soles must have both the expected
  bottom and top heights, which checks their orientation as well as contact.
- Artifact tests decode all three APNGs and compare every frame byte-for-byte
  with its sheet cell; master cells must also equal retained render PNGs.
  All frames need transparent margins. GLB must contain skinned meshes and a
  one-second animation, not just a static preview. New checks cover closed,
  connected trouser skins and the exported eye/trouser color attributes.

These checks establish attachment and export behavior. They do not establish
final art quality, compelling run timing, or collision-free garments. The
upper body remains fairly rigid, recovery is broad and rounded, and the
procedural model has a soft sculpted finish rather than painted folds/fur.
The original mouse remains the style target; user review of v3 is pending.
No production assets or runtime code are changed.

The initial `preview-v3` is retained as a rejected bind-pose failure: setting a
matrix on a zero-length edit bone did not establish the intended bone
direction. Skeleton endpoints posed correctly while the inverse bind matrices
rotated the artwork incorrectly. Explicit rest endpoints and roll, verified
before binding, fix that failure in `preview-v4` and `v1`. The initial sole
  minimum-height check alone did not detect a vertically rotated sole; the
  additional top-height check is deliberate evidence of the correction.

The first hood-down full render remains in `hood-down-shadow-draft/`.
Its close-up exposed wide, dark wedges from Eevee's screen-space contact
shadows around subpixel whiskers. The final render disables those contact
shadows while retaining ordinary shadow maps and ambient occlusion. A paired
close-up check uses identical geometry and camera. The earlier three-pose
model check and the shadow comparison live in `hood-down-preview-v1/`.
