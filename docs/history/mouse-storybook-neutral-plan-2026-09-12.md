> Archived 2026-09-12 after the user accepted the storybook 3D mouse and
> selected its workflow as the plan of record. Statements below about current
> work, next steps or pending acceptance describe the earlier state.
> See the [current plan of record](../mouse-3d-plan.md).

# Storybook mouse: design and modeling direction

2026-09-12. The user supplied three new references and selected **image 2,
the storybook adventurer**, as the redesign anchor. This is a change in
character design, not another width adjustment to the existing model.

**The user explicitly approved this plan** in the following message and supplied
the original [Reddit post](https://www.reddit.com/r/blender/comments/1s6s4dg/first_model_and_look_for_some_tips/).
No additional files or art-direction decisions are required to begin the
neutral-character milestone. The render machine remains reachable with Blender
4.0.2 and its 24GB RTX 3090. The next useful user input is visual feedback on
that neutral model.

## Source-model lookup

The Reddit author is `Mysterious-Tap8697`. The post describes a first modeling
project and plans for later rigging/animation. No model download or `.blend`
link was found in the retrieved post and visible comments; focused searches
did not locate a matching downloadable asset. The author profile could not be
retrieved, so this is not a claim that no public file exists anywhere.

The author confirms that the colored reference drawings are AI-generated and
acknowledges mismatches between views. Treat them as appearance input and
reconcile geometry deliberately. The comments are third-party opinions, not
project instructions. No messages have been sent to the creator.

The source file is therefore an optional starting point, not a dependency.
Proceed using the selected storybook direction and the retained concept, while
validating front/profile/three-quarter consistency in actual 3D.

[Selected reference](../../experiments/mouse_3d/storybook-direction-v1/reference-02.png) ·
[First concept sheet](../../experiments/mouse_3d/storybook-direction-v1/concept-sheet.png) ·
[Exact generation prompt](../../experiments/mouse_3d/storybook-direction-v1/prompt.txt)

The concept is a 2D visual proposal generated with the built-in imagegen tool.
Its three views are not a geometrically verified turnaround. Finger shapes,
tail silhouette and small outfit details need reconciliation before modeling.
No new 3D model, rig or animation is claimed by this concept artifact.

## Implementation checkpoint

The user explicitly requested implementation, and the
[first neutral 3D study](../../experiments/mouse_3d/storybook-neutral-v1/README.md)
is now ready for visual review. It contains a new generated/cleaned head and
body, actual clay/color views, a temporary body rig, two deformation checks
and sprite-size exports. The [implementation record](../../experiments/storybook_mouse/README.md)
retains inputs, exact model/code versions, raw output, cleanup, failures and
working source snapshots.

Ten focused checks pass. Artistic acceptance remains open. The next work is
to assess the neutral silhouette and face, then refine the hands, rough tufts
and facial mesh for expression controls. The current temporary binding is not
a production rig; separate clothing and final locomotion remain later steps.

## What the references change

| Reference | Useful direction | Relationship to this character |
| --- | --- | --- |
| [1: soft cream mouse](../../experiments/mouse_3d/storybook-direction-v1/reference-01.png) | Integrated anatomy, short muzzle, clear hands/paws, soft forms | Body/anatomy reference; the user selected a different identity/palette |
| [2: storybook adventurer](../../experiments/mouse_3d/storybook-direction-v1/reference-02.png) | Oversized ears, expressive eyes, cheek tufts, brown/cream fur, green cowl and simple leather equipment | Primary design anchor, selected by the user |
| [3: expressive game hero](../../experiments/mouse_3d/storybook-direction-v1/reference-03.png) | Modeled eyelids and brows, expression, distinct material treatment, deliberate silhouette | Facial/art-finish reference; its outfit and colors are not the selected design |

These are different characters and camera views. They must not be treated as
three orthographic drawings of one body, or as interchangeable pose controls.

Our existing model establishes deterministic skinning and sprite export.
Its code-defined head profiles, shallow surface eyes and mostly human clothing
silhouette are limiting the design. Matching the new reference calls for a
dedicated character mesh. Increasing subdivisions, adding surface noise or
widening the same profile does not by itself create cheek planes, expressive
eyelids or convincing hands.

## Target design

- Warm brown fur, a cream facial mask and belly, a green lowered hood/cowl,
  and a restrained leather belt/pouch/wraps outfit.
- A short, broad face with a small nose, deliberate cheeks/jaw and modest fur
  tufts. Eye sockets, upper/lower lids, brow forms and mouth corners are part
  of the facial construction. The eyes show controlled whites, irises and
  pupils, rather than depending on shiny side-mounted discs.
- Large ears that contribute strongly to the silhouette, with shaped roots,
  varying thickness and an inner bowl.
- A complete body with readable shoulders, hips, hands and broad mouse paws.
  The selected reference's wraps and paws change the lower-leg/foot design;
  do not force it into the existing human boot mesh or assume the current
  run fits without retargeting.
- Fur masses and paint carry detail without requiring particle fur. Cloth,
  leather and exposed fur should read as different materials under restrained
  lighting. The green cowl stays distinct from the body mesh.

## Sequence

1. **Resolve the design sheet.** Compare the new concept with image 2 and
   establish consistent front/profile head and body landmarks. Keep the
   selected reference as the design authority when generated views disagree.
   Avoid tracing inconsistent generated details into a rig contract.
2. **Build a neutral head and underlying body.** Establish the silhouette in
   front, profile and three-quarter views. Model eye sockets/lids, cheeks,
   jaw and paws explicitly. First review at 512px and the proposed 96/128px
   sprite sizes. This is the next useful 3D milestone.
3. **Produce a deformable mesh.** Sculpt or edit the forms, then create a clean
   polygon layout around facial features and bending joints. Keep clothing
   separable from the underlying character. Add basic blink/smile shapes once
   the facial mesh is stable.
4. **Fit the rig to the new anatomy.** Reuse the rig-building and validation
   approach, but author new proportions and paw-contact semantics. Check one
   extended pose and one folded pose before spending work on a full cycle.
5. **Add the simple outfit and materials.** Preserve the head/ear/hand read;
   keep accessory detail secondary. Use authored color/roughness and baked
   detail where useful. Tiny stitching is not a substitute for clear shapes.
6. **Reuse the sprite export pipeline.** Fixed camera, origin, frame size,
   transparent render masters, consistent palette, and comparisons at target
   sprite resolutions. No engine-format change is needed to explore this.

The near-term deliverable is a convincing neutral character plus two posed
checks, rather than automatically rendering another full run for each small
model edit. The existing v1/v2/v3 assets and their evidence remain intact.

## How to obtain the base mesh

**Direct Blender modeling/sculpting** gives the most control over face design,
separable clothing and animation topology. The artistic work is real: code is
useful for construction and rig/render automation, but more parametric edits
alone should not be represented as a reliable route to reference 3's finish.
An existing editable source model, if available from the supplied reference,
would also be worth evaluating as a starting point.

**An AI-generated mesh can be a candidate starting shape.** A clean image of
one neutral character is a better input than the supplied screenshot containing
a gray model, two drawings and Blender UI. Inspect generated anatomy and
surface connectivity before binding it; expect targeted cleanup or a new
animation-friendly polygon layout. Generated texture and attractive preview
lighting do not demonstrate usable eyelids, hands, separate garments or a rig.

The official [Hunyuan3D-2 repository](https://github.com/Tencent-Hunyuan/Hunyuan3D-2)
lists 6GB VRAM for shape generation and 16GB for shape plus texture generation.
That makes it a plausible bounded pilot for the 24GB RTX 3090, subject to
dependency and available-memory checks. This is published capacity guidance,
not a measured result on this machine.

[TRELLIS.2](https://github.com/microsoft/TRELLIS.2) is another image-to-3D route,
with PBR material output. Its official requirements specify at least 24GB and
report verification on A100/H100; its timing table is measured on H100. Do not
promise those timings or verified 3090 compatibility. A first local pilot should
favor a bounded, isolated setup over modifying the working ComfyUI environment.

The recommended path is the selected design → neutral sculpt/base-mesh
candidate → explicit cleanup and rig fitting → the existing sprite exporter.
The base mesh's artistic quality, especially the face, is the next thing to
demonstrate. Hardware alone will not resolve that design task.

## Retained planning evidence

- Three original reference files retained unchanged.
- User selection of image 2 retained here as the art-direction decision.
- One built-in imagegen request, using only reference 2. Exact prompt and input
  were displayed before submission; raw output is retained as `concept-sheet.png`.
- No Blender/model edits, image-to-3D calls, package installs, production imports
  or runtime changes ran. No implementation tests were needed for this design
  discussion and concept artifact.
