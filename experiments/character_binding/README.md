# Character-binding experiment

Current priority: repair the green-coated mouse's twelve-frame run. The
[approved experiment plan](../../docs/sprite-run-experiment-plan.md) and
[handoff](../../docs/handoff.md) own the current work. Old experiment gates are
historical; the actual source, skeleton, posed artwork and conditioning maps
must be shown to the user before generation.

`puppet_documents/mouse_run_reference_v2.json` is the current candidate. Its
twelve target poses come from the user's `inputs/run-pose-reference-12.png`,
traced in `inputs/run-pose-trace-v1.json`. The older rig motion and
`mouse_run_calibrated_v1.json` were rejected as the pose authority. The source
image remains the separate bind pose. The candidate retains the original
running artwork, corrected near/far arms, rigid boots and a bound tail.

`retarget_frames` matches a chosen gait phase to the source drawing and carries
bone-angle changes through the clip; `rebase_frames` only centers it. Neither
command can decide which gait phase or limb the drawing depicts.

To reproduce the current target poses, run `scripts/retarget_run_reference.py`
with `--document`, `--trace` and `--output`, then apply its emitted commands
with `puppet_edit`. Render once, then use `--ground-from-render` to emit the
support/flight registration commands and apply those before the final render.
The transfer uses one common scale for both legs across all frames, preserving
the reference's projected proportions. The four thigh/shin bones explicitly
scale from the foreshortened bind artwork. This choice and the phase
interpretation need visual review.

`scripts/render_puppet_review.py` consumes the actual renderer output and the
optional `--trace` / `--pose-sheet` pair. It writes a self-contained review;
its grayscale layer-order view is a diagnostic, not physical depth.

## Active boundary

| Stage | Owner |
|---|---|
| Subject isolation and topology | C++ `IsolateSubject` and `profile_silhouette` |
| Explicit layered-puppet source, rendering, and evidence | C++ `layered_puppet` and `render_layered_puppet` |
| Imported frame processing and persistence | Existing C++ frame-set pipeline |
| Blender-authored historical 3D sources | Blender Python API adapters |
| See-through semantic-layer inference | External offline PyTorch adapter; never an engine dependency |

Rejected semantic inference and ComfyUI animation-control prototypes were
removed rather than ported. Python remains only where Blender or an external ML
runtime requires its own Python host.

## Layout

```text
experiments/character_binding/
  render_mouse_production.py
                        Blender adapter for the current shipped source
  render_character_family.py
                        Blender adapter for body-plan evidence
  inputs/              tracked artwork and rigs the tools consume
  character_specs/     Blender family inputs
  puppet_documents/    authored puppet documents; the file a person keeps
  puppet_specs/        remaining legacy specs; See-through only, being retired
  evidence/            committed records of closed experiments
  out/                 generated renders; ignored whole, regenerable
  README.md
  FINDINGS.md

src/artwork/layered_puppet.{h,cc}
src/artwork/layered_puppet_spec.{h,cc}
src/artwork/puppet_document.{h,cc}
src/artwork/puppet_document_json.{h,cc}
src/artwork/puppet_build_json.{h,cc}
src/artwork/puppet_editor_session.{h,cc}
src/artwork/puppet_editor_workspace.{h,cc}
src/artwork/puppet_editor_page.{h,cc}
src/artwork/puppet_editor.html
src/common/loopback_http_server.{h,cc}
src/artwork/layered_puppet_diagnostics.{h,cc}
src/artwork/semantic_layer_import.{h,cc}
src/artwork/skeleton_rig.{h,cc}
scripts/render_layered_puppet.cc
scripts/puppet_edit.cc
scripts/serve_puppet_editor.cc
tests/artwork/layered_puppet_test.cc
tests/artwork/layered_puppet_spec_test.cc
tests/artwork/puppet_document_test.cc
tests/artwork/puppet_document_json_test.cc
tests/artwork/puppet_build_json_test.cc
tests/artwork/puppet_editor_session_test.cc
tests/artwork/puppet_editor_workspace_test.cc
tests/artwork/layered_puppet_diagnostics_test.cc
tests/artwork/semantic_layer_import_test.cc
tests/artwork/skeleton_rig_test.cc
```

See-through was evaluated from an isolated temporary checkout on `derry`. Its
accepted layers are tracked in `inputs/see-through-v1/`; the virtual
environments and model caches that produced them are not repository
dependencies.

`inputs/` and `out/` are the split that matters. Everything in `inputs/` is
tracked and either irreplaceable or expensive to reproduce: the approved mouse,
the isolated running source whose coordinates own the cutout masks, the
See-through layers, and the 23-point rig. Everything in `out/` is a render a
tool wrote and can write again, so the whole directory is ignored. A new file
goes in whichever one answers "would losing this cost anything?"

## Finding the latest run

`out/` holds every render this experiment has produced, and its names sort
badly: `blender-proxy-v10` lands before `blender-proxy-v2`, and unrelated
families interleave. To see what was made most recently:

```bash
ls -t experiments/character_binding/out | head
```

Name new output directories with a number, `042-immutable-coat`, so ordering is
visible in a plain listing too. The tools take the directory as `--output`, so
this is a convention rather than something they enforce. Renaming anything under
`out/` is free: nothing there is tracked.

## Build the C++ proof tools

```bash
cmake --preset dev
cmake --build build/dev --target extract_profile_silhouette \
  render_profile_pose_control render_profile_pose_depth \
  render_profile_deformation render_layered_puppet puppet_edit serve_puppet_editor
```

Authoring happens in one place. From the repository root:

```bash
build/dev/bin/serve_puppet_editor
```

Open `http://127.0.0.1:8770/`. Every flag has a default pointing at the
directories above, so the editor needs none of them; pass `--document_root`,
`--asset_root`, `--rig_root` or `--port` when a second character wants its own.
Section 13 lists them. Keep the server bound to loopback.

Evidence renders still come from `render_layered_puppet`:

```bash
build/dev/bin/render_layered_puppet \
  --source=experiments/character_binding/inputs/interactive-run-source-v1.png \
  --document=experiments/character_binding/puppet_documents/mouse_interactive_run_v1.json \
  --output=experiments/character_binding/out/interactive-run-v1 \
  --frame_size=48 \
  --zoom=8
```

## 1. Isolate and extract topology in C++

Generated near-white backgrounds in the current fuzz set require an explicit
matte tolerance of 128. This is experiment evidence, not a new production
default; future references should request transparency or a truly flat matte.

```bash
build/dev/bin/extract_profile_silhouette \
  --input=profile-reference.png \
  --isolated_output=experiments/character_binding/out/profile/isolated.png \
  --output=experiments/character_binding/out/profile/skeleton.png \
  --control_output=experiments/character_binding/out/profile/control.png \
  --background_distance=128
```

Outputs answer distinct questions:

- `isolated.png`: did production isolation recover one usable subject?
- `skeleton.png`: does the deterministic medial axis preserve its topology?
- `control.png`: is the neutral contour/axis suitable as binary edge control?
- console diagnostics: components, endpoints, and branch pixels for fuzz comparison.

## 2. Closed generation-control gates

Canny-only, ordinal-depth-only, and combined Canny/depth diffusion controls all
failed the bounded neutral/contact/passing/airborne pose gate. Weak structural
control preserved identity but ignored elevation and limb order; strong control
obeyed pose while destroying identity and pixel style. The Python ComfyUI
orchestration and workflow templates were removed after the stop rule fired.
Measured settings, digests, and visual findings remain in `FINDINGS.md`.

Skeleton conditioning closed the same way on 2026-09-06. Three requests asked
for compressed-support, contact, and high-recovery poses; all three came back as
conventional split strides, and two that should have looked very different
stayed 0.745 similar. The model has learned that running means legs far apart,
so it draws that and ignores a skeleton asking for legs together. Two runs, same
wall. The skeleton PNG renderers and the HTML review page were deleted with it;
`skeleton_rig` keeps only the parser and the cycle metrics, which the
interactive editor needs.

Generation is no longer asked to infer a pose. Section 10 poses the character
directly and asks only for cleanup.

## 3. Direct C++ deformation gate

`profile_deformation` inverse-maps the target layer map into the isolated source.
Pixels keep their primary bone layer; transforms blend near shared joints, and
sampling never crosses into another layer.

```bash
build/dev/bin/render_profile_deformation \
  --source=out/profile-binding/source-color.png \
  --source_layers=out/profile-binding/pose-neutral-layers.png \
  --target_layers=out/profile-binding/pose-contact-layers.png \
  --binding=out/profile-binding/binding.json \
  --pose=contact \
  --output=out/direct-deformation/contact.png
```

Neutral reproduces every source pixel exactly: 18,974 mapped, zero unmapped, and
zero differing pixels. Contact maps all 18,852 requested pixels and removes
forward-splat holes, but the visible boot/coat intersection remains. Smooth
sampling cannot repair incorrect target ownership or invent legs hidden in the
neutral coat. The renderer passes; the automatically inferred layered source
fails the contact-pose visual gate.

The next input experiment needs separated limb artwork—either a bind-pose
reference with visible limbs or fixed supplemental rear/front limb patches—not
another deformation algorithm over the same occluded source.

## 4. Direct low-poly 3D gate

The initial Blender 4.0.2 spike on `derry` established that one reusable model
could preserve identity while changing pose. It used a fixed orthographic
camera, emission-only materials, one sample, a 0.01 filter, and enlarged backing
geometry for pixel-stable edges. Neutral and contact retained identical head,
ears, muzzle, hood, scarf, coat, belt, tail, and materials.

Structural and pixel-discipline verdicts passed. The art-direction verdict did
not: the face was minimal, hands were tiny, and legs read too human. That result
closed the structural gate and became the input to the production renderer
below; the obsolete two-pose proxy entry point was removed.

## 5. Generated model-sheet mapping

The generated profile was used as a native model sheet rather than projected
onto the mesh. Its 30×44 visible bounds drove a 27×45 neutral model at 75.5%
registered silhouette IoU. The comparison isolated the remaining authored work:
cheek and eye construction, coat lapels and pockets, hands, and mouse-like
boots. Keeping the pixels off the mesh preserved real hidden geometry for later
poses.

## 6. Reusable body-plan automation

`render_character_family.py` separates reusable model construction from
character-specific values. Version 1 defines three body plans:

- `biped`: shared humanoid and mouse skeleton, neutral stance, and stride;
- `quadruped`: shared badger, rabbit, fox, and cat skeleton, neutral stance,
  and run pose;
- `flyer`: shared bat body and neutral/action wing articulation.

The seven committed JSON specs select proportions, camera framing, palette,
ear type, tail type, and limited species traits. One Blender invocation validates
and renders every spec to its own output directory:

```bash
blender --background --python \
  experiments/character_binding/render_character_family.py -- \
  --spec-dir experiments/character_binding/character_specs \
  --out /tmp/zebes-character-families
```

Each specimen produces `neutral.png`, `action.png`, both source `.blend` files,
and a manifest at 48×48. The reviewed matrix uses columns humanoid, mouse,
badger, rabbit, fox, cat, bat; neutral is the first row and action the second.
All seven stay inside frame, change pose, and remain species-readable. Long ears
identify the rabbit, the brush tail and palette identify the fox, pointed ears
plus a thin tail identify the cat, and wing topology identifies the bat. The
badger remains the weakest read because its identity depends mainly on body mass,
round ears, and a face stripe.

The reuse boundary is deliberate:

- a new character within these families should usually be one JSON spec;
- a recurring ear, tail, marking, garment, or limb feature belongs in the
  corresponding shared builder;
- a genuinely different topology needs one new body-plan builder;
- production-quality faces, hands, clothing, species anatomy, and authored
  motion remain modeling work rather than spec values.

This is evidence that body-plan automation removes repeated rig/render setup.
It is not evidence that seven production character models can be generated from
names or palettes alone.

## 7. Production mouse player

`render_mouse_production.py` implements the art work identified by the prior
gates. It renders six complete 48×48 RGBA source sheets from one model:

- four-frame left/right idle loops at 15 ticks per frame;
- eight-frame left/right run loops at 4 ticks per frame;
- four-frame left/right airborne sequences with hold-last playback.

The model has a distinct eye patch and glint, cheek and muzzle planes, ear
highlights, hood, scarf, asymmetric coat panels, lapels, pockets, cuffs, belt,
larger hands, highlighted boots, and a posed tail. All frames retain a
transparent border. Grounded frames meet the authored contact line; airborne
frames retain a visible gap.

```bash
blender --background --python \
  experiments/character_binding/render_mouse_production.py -- \
  --out /tmp/zebes-mouse-production

build/dev/bin/import_animation_frame_sets \
  --asset_root=assets \
  --manifest=/tmp/zebes-mouse-production/import.json
```

The renderer owns stable Texture, Sprite, and recipe IDs. The importer validates
the manifest, retains every source sheet as imported artwork, runs the production
`AnimationFrameSetPipeline`, and publishes all six state bindings through the
transactional API. A later clip failure rolls back earlier clips in reverse
order.

The production Blueprint keeps the existing stable player ID and 32×64 collider.
Catacombs entity 4 now resolves the new idle Sprite, while runtime state
transitions select the other five clips. `SpriteReviewer` publishes native and
enlarged frames, a contact sheet, an ordered strip, origin/contact/bounds
alignment, every adjacent-frame difference, loop closure, and airborne
hold-final evidence.

The engine has no normal-map field on Texture or Sprite definitions. This asset
therefore ships color frames only rather than introducing an unused sidecar
format.

## 8. Explicit layered 2D puppet

The C++ `layered_puppet` library tests the source contract rejected by the
flattened-image deformation gate. One explicit spec owns bones, joints, per-part
source masks, hidden-surface underpaint, and pose-specific draw order; no
inferred semantic ownership crosses into rendering.

```bash
build/dev/bin/render_layered_puppet \
  --source=experiments/character_binding/inputs/profile-binding-deformation-v2/source-color.png \
  --document=experiments/character_binding/puppet_documents/mouse_profile_v1.json \
  --output=experiments/character_binding/out/layered-puppet-cpp
```

The bounded output contains ten separated parts plus neutral, contact, passing,
and airborne working/native frames. The first mouse proof retains the generated
head, face, hood, scarf, and coat at 48px while producing four distinct poses.
Its neutral silhouette IoU against the source is 95.3%; the airborne frame
finishes four pixels above the grounded contact band. Focused Catacombs review
at 0.5x, 1x, and 2x reports no objective findings.

This passes the direction gate, not final animation acceptance. Rigid arm and
leg pieces still expose joint seams and source-paint contamination. The next art
input should be a genuinely separated painted sheet with overlap under each
joint; more automatic ownership inference over the flattened reference is not
the fix.

## 9. See-through layer-decomposition gate

The external [See-through](https://github.com/shitagaki-lab/see-through) V3
model was run once from an isolated temporary environment on the approved mouse.
It completed both RGBA decomposition and pseudo-depth inference on `derry`.

This is a partial pass. Its two completed arm layers, combined boot layer, and
coat layer preserve useful style and hidden surfaces. Leg, tail, and bottomwear
layers are empty; ear and hair classes hallucinate human anatomy. See-through
therefore remains an offline candidate generator, not a trusted parser or an
engine dependency. C++ must map and validate accepted RGBA layers, split the
boots, preserve original visible pixels, and reject every semantically invalid
class before articulation.

## 10. Skeleton-driven semantic arm

The C++ semantic importer restores the accepted See-through arm crop, reduces it
to the 256px working canvas, and pastes original visible pixels back exactly.
`mouse_immutable_coat_v1.json` binds the accepted arm to
shoulder/elbow/wrist while importing See-through `topwear` as immutable
coat-without-arms artwork.

```bash
build/dev/bin/render_layered_puppet \
  --source=experiments/character_binding/inputs/profile-binding-deformation-v2/source-color.png \
  --spec=experiments/character_binding/puppet_specs/mouse_immutable_coat_v1.json \
  --semantic_root=experiments/character_binding/inputs/see-through-v1/optimized \
  --output=experiments/character_binding/out/semantic-arm-immutable-coat-v1
```

The relationship-stretch candidate was rejected: the generated coat was already
correct, and adding 592 pixels made it too wide. The immutable proof saves the
imported coat beside the final part and gates decoded RGB, alpha additions,
alpha removals, and digest equality.

Source and final coat digests match
(`0400b5084a83957f727c2292d52f481f1af07e29331c628b07c69c2561351fc4`);
changed pixels, added alpha, and removed alpha are all zero. Neutral remains
exact. Passing keeps the reachable bent pose and casts a separate 288-pixel
shadow without mutating the coat.

The old full-arm backfill metric reports 745 uncovered pixels but is deliberately
not a gate: pixels outside the approved coat silhouette may reveal background.
Existing blockers remain 149 body-visible orphan pixels, four airborne folds,
and contact/passing hole counts of 355/177 against 174 neutral.

## Next gate

Review the immutable coat alone, arm-hidden body, moved-arm tint, shadow tint,
and native passing frame. If the coat is accepted, keep it immutable; then
separate the tail and clear the orphan/fold gates.

When reviewing, remember only 3 of the 10 bones drive a part. The legs and head
do not move; the four poses are a standing mouse with one arm moving.

## 11. Headless puppet authoring

`puppet_edit` builds and edits a puppet document without a browser. A document
holds the source image, the skeleton, the outlines cut from that image, and the
frames those layers are posed in; it is the file a person keeps. The layered
puppet spec is derived from it and is not stored.

```bash
cmake --build build/dev --target puppet_edit
build/dev/bin/puppet_edit \
  --create \
  --document=/tmp/mouse-run.json \
  --commands=/tmp/edits.json \
  --rig_root=experiments/character_binding/inputs \
  --emit_spec=/tmp/mouse-run-spec.json
```

`--commands` is a JSON array; `-` reads it from stdin. Omit it to re-check an
existing document. `--dry_run` reports without writing. Every run prints the
joint, bone, part, and frame counts and either `ready to build` or the next
thing missing, in the order the editor asks for it.

```json
[
  {"command": "set_source_image", "path": "run.png", "width": 256, "height": 256},
  {"command": "import_skeleton", "rig_path": "rig-bench.json", "clip_id": "run",
   "import_frames": true},
  {"command": "add_frames", "name_prefix": "run", "count": 12, "copy_from": ""},
  {"command": "add_part", "name": "front_leg", "bones": ["hip_c-knee_l", "knee_l-ankle_l"]},
  {"command": "set_part_outline", "part": "front_leg", "outlines": [[[100, 150], [120, 150], [110, 200]]]},
  {"command": "pose_joint", "frame": "run_01", "joint": "knee_l", "point": [200, 158],
   "scope": "all_frames"}
]
```

The commands are `set_source_image`, `scale_to_size`, `set_guide_image`,
`import_skeleton`, `add_joint`, `remove_joint`, `set_joint_chain`,
`move_rest_joint`, `add_bone`, `remove_bone`, `set_bone_stretch`,
`set_frame_rate`, `set_allow_overlap`, `add_frames`, `remove_frame`,
`reorder_frames`, `rename_frame`, `rebase_frames`, `fit_bone_lengths`,
`set_anchor_frame`, `pose_joint`, `add_part`, `remove_part`, `rename_part`,
`set_part_bones`, `set_part_outline`, `set_part_exclude_outlines`,
`set_part_exclude_parts`, `set_part_fills`, `set_part_mesh`, and
`set_draw_order`. Every field is required and an unknown field is rejected.

### Rigid bones and stretching ones

The renderer places a part's pixels at the distance from the joint they were
drawn at and turns them to the posed bone's direction. It does not scale. A
frame that shortens a bone therefore leaves the artwork sticking past the posed
end joint: the skeleton says one thing and the drawing says another.

`set_bone_stretch` opts one bone out of that. On, the artwork is stretched along
that bone to match its posed length, so a shorter bone really is a shorter limb.
Only the distance along the bone scales, so the limb gets longer rather than
fatter. A skinned two-bone chain scales uniformly about the shared joint
instead, because its two bones point different ways and there is no one axis.

Off is the default and is what every render has always done. Leave it off unless
a limb is meant to change length. The Frames step reports the worst bone stretch
across the clip, which is how a bind pose that drifted after the frames were
posed becomes visible.

### Overlap

`set_allow_overlap` decides whether two parts may own the same source pixel.

True, the default, lets them: a coat and the body under it can share pixels on
purpose. Both parts draw those pixels and carry them in different directions, so
they tear in motion. The editor lists every such pair with how many pixels it
is.

False settles ownership by declaration order. A part gives up whatever a part
declared before it already claimed, so a shape drawn across an existing part
keeps only the free side, right up to that part's edge. It changes no outline
already on disk: turning it back on restores the overlap exactly.
`set_part_exclude_parts` is still there for giving up pixels one pair at a time.

The editor also snaps while you draw, so the line you see is the line you get: a
point clicked inside a part added before this one moves out onto that part's
edge. That is a change to the outline, made as you click it, not to what is
already stored. It moves points, not segments — a straight run between two
points that are both outside can still cut a corner off a claimed part, and the
ownership rule above is what takes those pixels back.

`scale_to_size` is what makes artwork at another resolution usable. A build
refuses a document whose width and height are not the source PNG's, so pointing
a 256-pixel puppet at a 512-pixel drawing would otherwise mean retracing every
outline. It multiplies every rest joint, posed joint, outline point, fill point,
mesh spacing and blend radius by the size change, and takes the new size. Send
it in the same batch as the `set_source_image` beside it so the two cannot
disagree on disk.

Every joint carries a `chain`: the limb it belongs to, such as `arm_l` or
`spine`. Nothing in the drawing reads it. A Rig Bench file records one per
point, so a skeleton written out of a document needs it, and no bone graph
recovers it afterwards — the mouse rig is one connected graph and still has
seven limbs. `set_joint_chain` moves a joint to another limb without moving the
joint.

`rename_part` changes a label. `set_part_bones` changes which bones bend the
pixels. They are different repairs and the second is the one that matters: a
part traced over the screen-left arm but built on `shoulder_l-elbow_l` throws
those pixels across the body in every frame, and no name fixes that. The
anatomical left of this rig is screen-right, which is exactly how the mistake
gets made.

Which pixels a part owns is decided in three passes: `set_part_outline` claims a
region, `set_part_exclude_outlines` carves holes out of it, and
`set_part_exclude_parts` gives up whatever the named parts already claimed. A
part may only exclude parts declared before it. `set_part_fills` paints hidden
surface underneath, with the colour stored outright rather than as a point to
sample, so repainting the source cannot change what a fill means.

Three behaviours are worth knowing before authoring:

- `pose_joint` with `"scope": "all_frames"` offsets that joint in every frame by
  the same amount, so each frame keeps its own relationship to the one being
  authored. `"scope": "frame"` moves only the named frame.
- `import_skeleton` names bones `<start>-<end>` from the rig's topology, and
  takes the clip's first frame as the rest pose because the Rig Bench format
  has no rest pose of its own. It is refused once parts exist, since parts name
  bones the import would replace.
- `set_guide_image` chooses a backdrop to trace joints onto when a skeleton is
  drawn from scratch. It is a drawing aid: nothing reads its pixels, it never
  reaches a build, and an empty path clears it. This is the field the browser
  editor will draw under the canvas; the guide artwork itself does not exist
  yet.

Twelve frames from one base skeleton is `import_skeleton` with
`"import_frames": false` followed by `add_frames` with `"count": 12`.

### Authoring a rig on its own

A rig is a skeleton with no drawing attached: joints, the limb each belongs to,
bones, and one clip of poses. `--emit_rig` writes one, and it needs no source
image, no parts and no frames, so a skeleton can be drawn from nothing and
reused on every character after it.

```bash
build/dev/bin/puppet_edit \
  --create \
  --document=/tmp/biped.json \
  --commands=/tmp/skeleton.json \
  --emit_rig=experiments/character_binding/inputs/biped-v1.json \
  --rig_clip=authored
```

The document's frames become that clip. A document with no frames yet writes a
one-frame clip holding the rest pose, which is exactly what `import_skeleton`
reads a rest pose from, so the rig comes straight back:

```bash
build/dev/bin/puppet_edit --create --document=/tmp/mouse.json \
  --rig_root=experiments/character_binding/inputs \
  --commands=<(echo '[{"command": "import_skeleton", "rig_path": "biped-v1.json",
                       "clip_id": "authored", "import_frames": true}]')
```

Importing `rig-bench.json` and writing it straight back out reproduces its
points, chains, bones, clip rate and every pose exactly. Only `floor_y` differs:
a document has no ground of its own, so the written rig takes the lowest joint.
Nothing reads that field.

## 12. Retiring the spec format

A puppet document is what a person keeps; the layered puppet spec is derived
from it in memory and handed to the builder. Two formats is one too many, so
the specs are being retired file by file rather than left as a second way to
say the same thing.

`mouse_interactive_run_v1` and `mouse_profile_v1` are documents now. Both were
migrated by resolving each fill's sampled point to an outright colour, and both
render byte-identically to what their spec rendered: same frames, same parts,
same manifest. The migration script was deleted after it ran, and
`puppet_document_json_test` loads every tracked document so a later format
change cannot quietly break one.

Three specs remain, all See-through:

| Spec | Why it cannot be a document yet |
|---|---|
| `mouse_semantic_arm_v1` | `semantic_tag`, `source_from_semantic_reach`, `stretch_to_cover_parts`, shadows, five review gates |
| `mouse_immutable_coat_v1` | the same, plus `immutable_semantic_layer` |
| `mouse_interactive_limbs_v1` | the same, plus `semantic_component` |

**Remaining cleanup, in order.** Do this once the See-through gate closes,
because migrating a spec whose approach is still under review would migrate
work that may be thrown away.

1. Add the semantic fields to the document: `semantic_tag`,
   `semantic_component`, `clip_to_source_alpha`, `source_from_semantic_reach`,
   `immutable_semantic_layer`.
2. Add `stretch_to_cover_parts`, `shadows`, and the five remaining `require_*`
   gates, each one when the spec that needs it migrates.
3. Migrate the three specs, proving each renders byte-identically first.
4. Delete `puppet_specs/`, `layered_puppet_spec`'s JSON front door, and
   `render_layered_puppet --spec`. The builder keeps its pixel work; only the
   410-line JSON reader goes.

Until then, both flags exist and `render_layered_puppet` takes exactly one of
`--spec` or `--document`.

## 13. Serving a puppet document

`serve_puppet_editor` is the only puppet editor. It serves a directory of
documents, holds one of them open, and edits it with the same commands
`puppet_edit` takes, so the browser and an agent drive identical code.

Run it with nothing:

```bash
cmake --build build/dev --target serve_puppet_editor
build/dev/bin/serve_puppet_editor
```

| Flag | Default |
|---|---|
| `--document_root` | `experiments/character_binding/puppet_documents` |
| `--asset_root` | `experiments/character_binding/inputs` |
| `--rig_root` | `experiments/character_binding/inputs` |
| `--port` | `8770` |
| `--document` | none; the editor starts with nothing open and asks |
| `--page` | none; the embedded page is served |

The defaults assume the working directory is the repository root. When
`--asset_root` or `--rig_root` is not a directory the server says so and stops,
rather than serving an editor with nothing to choose from. Naming only
`--document` still gives a directory to browse: the one that file sits in.

Document names are a file name inside the root, letters, digits, `_` or `-`,
ending `.json`. Anything else is refused, so a name from the browser cannot
reach a file outside the directory. A new document is empty and valid; it simply
cannot build yet and says so.

A document that will not build still opens. Only a file that will not parse is
refused. A puppet whose coordinates are not its picture's size, or whose picture
has been renamed, would otherwise be locked out of the one tool that can repair
it; instead it opens with no artwork and the reason on screen. An edit that
breaks the build is still refused whole, so that state cannot be created from
inside the editor.

| Route | What it does |
|---|---|
| `GET /api/session` | the open document, whether it builds, what it still needs, and what else can be opened |
| `POST /api/open` | opens another document in `--document_root` by file name |
| `POST /api/create` | writes a new empty document and opens it |
| `POST /api/commands` | applies a command array, saves, returns the same payload |
| `POST /api/save_rig` | writes the skeleton to `--rig_root` as a Rig Bench file |
| `GET /api/build` | the built geometry: bind skeleton, meshes, posed frames |
| `GET /parts/<name>.png` | one built part's artwork |
| `GET /source.png`, `GET /guide.png` | the document's own images |
| `GET /` | the page named by `--page`, once step 4 writes one |

`POST` requires an `Origin` of `http://127.0.0.1:<port>`, so a page on another
origin cannot drive an editor bound to this machine. A whole command array
either lands or none of it does; a rejected batch answers 409 naming the
command by position, and the file on disk is untouched.

Artwork is rebuilt only when the pixels would change. Moving a joint or adding
a frame skips the rebuild; changing an outline, a part, or the source image
forces one.

## 14. The editor page

Open `http://127.0.0.1:8770/`. The page is embedded in the binary; `--page`
serves a file instead, so the HTML can be edited and reloaded without a rebuild.

A document picker sits above the steps: choose another puppet to switch to it,
or type a name and press **New puppet** for an empty one. The name is letters,
digits, `_` or `-`; `.json` is added for you and typing it is harmless. That row
is rebuilt only when the list or the open document changes, so an edit elsewhere
does not empty a name half typed. Switching drops the mesh, the artwork and the
drawing in progress, because all of it belonged to the document being left.

Five steps across the top, in the order the data forces: **Source → Skeleton →
Parts → Frames → Export**. Each shows only its own controls and is green once
that stage holds what it needs.

- **Source** picks the artwork and the guide backdrop from what is on disk under
  `--asset_root`, and toggles either one on the canvas. Picking applies at once
  and stretches the whole puppet onto the new picture: `scale_to_size` and
  `set_source_image` go in one batch, so a swap to another resolution carries
  every joint, outline and fill with it instead of leaving them in the old
  coordinates. When the document's size and the file's size disagree — nothing
  can be built in that state — the step says so and offers the stretch as one
  button. The width and height boxes are still a deliberate override and move
  nothing when used.
- **Skeleton** imports a rig and clip from `--rig_root`, or builds one by hand:
  type a joint name and the limb it belongs to, click the canvas to place it,
  then join two joints into a bone. The limb stays in its box, so a whole arm is
  placed without retyping it. Dragging a joint moves the rest pose. Hovering one
  names it and gives its position.

  **Write this skeleton out as a rig** saves the joints, limbs, bones and poses
  to `--rig_root` as a Rig Bench file, and it appears in the skeleton list at
  once. It needs no artwork, so this step alone is the whole rig-authoring
  tool: make a puppet, place joints on the blank canvas or over a guide, save
  the rig, and every character after this one imports it. A rig of the same name
  is replaced.

  **Also update the puppets that imported it**, on by default, carries the
  change into every other document whose skeleton came from that rig. It adds
  joints and bones the rig gained and takes its limb names. It never moves a
  joint they already have — those were dragged onto that character's own drawing
  and are what ties the rig to the artwork — and it never removes anything. A
  document still using a joint the rig dropped is named and left alone rather
  than half-updated. A new joint arrives at the rig's own coordinates and has to
  be dragged into place. Bones are matched on the joints they run between, not
  their names, because a Rig Bench file stores no bone names.

  **Bone that may stretch** picks one bone and says whether its artwork scales
  when the bone's length changes. Off everywhere is the default and is what the
  renderer has always done.

  An imported rest pose comes from the clip's first frame and knows nothing
  about the artwork, so it will not sit on the character. Drag every joint onto
  the picture before drawing outlines: bind space is where the pixels are, and
  every frame bends away from it. Moving a rest joint leaves the frames alone.

  **Keep bone length while dragging** changes what a drag means. On, the joint
  swings around the joint it hangs from at the length it already had, and every
  joint below it turns by the same angle, so a leg bends instead of stretching.
  Off, the joint goes where the mouse goes and its bones change length. The
  same toggle is on the Frames step, where it does the same thing to a pose.

  Which joint hangs from which is read off the bones: a joint is the end of one
  bone and hangs from that bone's start. A joint that is the end of two bones,
  and the one joint that ends no bone at all, hang from nothing. With the toggle
  on, dragging one of those slides it and everything below it by the same
  amount, which is how a whole rig is moved onto the artwork without stretching
  the bone it is dragged by.

  A swing turns a whole chain, so one drag sends one command per joint it
  moved, in a single batch the server takes whole or refuses whole.
- **Parts** adds a part on one or two bones, changes which bones it follows,
  edits its mesh settings, and draws what it owns. Pick a part, then click the
  canvas to drop points; click the first point again or press Enter to close the
  shape, Backspace to take back a point, Escape to throw it away. The dropdown
  switches between claiming pixels and carving a hole out of what was claimed.
  Claimed shapes draw green, carved ones red.

  The selected part's own bones draw white and thick. Whether they lie inside
  the shape being traced is the whole question: a part built on the other arm's
  bones bends correctly-shaped pixels to the wrong place in every frame, and
  the still source picture never shows it. **Use these bones** re-points a part
  without touching its outline, fills or mesh settings. Renaming does not: a
  name is a label and the bones ignore it.

  The step also names every other part that claims a pixel this one claims, with
  how many. Both parts draw those pixels and carry them apart in motion; the
  still composite looks whole because that is exactly where the two still
  coincide. The checkboxes above give them up — a part may only give up what a
  part declared before it claimed, which is why only those are listed. **Parts
  may claim the same pixel** does the whole job at once: off, every part gives
  up whatever the parts before it claimed, and a point clicked inside one of
  those parts snaps out onto its edge as you drop it, so the outline on screen
  is the one the build will use. **Draw further back** and **Draw further
  forward** move the part through every frame's order at once, back to front, so
  whatever is drawn last is in front.
- **Frames** shows the bent puppet and adds frames from the rest pose or a copy,
  moves one earlier or later, renames it, sets the anchor, plays, and drags
  joints. The **moves every frame** checkbox is the difference between posing
  one frame and shifting that joint through the whole clip. A reorder sends the
  whole order and is refused unless it names every frame exactly once, so no
  shuffle can drop one. Renaming carries the anchor with it. **Frames per
  second** is what Play runs at and the rate a clip written out as a rig
  carries; it lives in the document, not the browser.

  The step reports the **worst bone stretch**: the bone furthest from its bind
  length over every frame. 1.00 means the frames still match the bind pose.
  A ratio far from 1 usually means a rest joint moved after the frames were
  posed, and every frame is now bent against proportions it was never drawn
  for — which nothing else on screen shows, because the artwork keeps drawing,
  just scaled along that bone.
- **Export** reports what the document holds and whether it builds.

What the page keeps to itself: zoom, which step is open, which frame is showing,
playback, and the selected part. That is one person's view of the work, so it
lives in browser storage and is never written to the document. Everything that
describes the puppet goes to the server as a command and is saved there, which
is why there is no save button.

A drag is previewed locally and committed once on release, so one gesture is one
saved document rather than a hundred.

Source, Skeleton and Parts all work in bind space: the flat source artwork and
the rest pose, which is where the pixels actually sit and what every outline and
joint position is written against. Showing a posed frame there would have
someone trace an outline onto a limb that has moved.

Frames and Export show the posed puppet: each part's artwork bent by its bones
for the current frame, so playback is the real animation rather than a moving
skeleton.
The mesh comes from `GET /api/build` and is refetched only when the server says
it rebuilt, which is why dragging a joint costs no transfer. Turn the preview
off to see the flat source underneath.

## Tests

```bash
scripts/test.sh profile_silhouette_test
scripts/test.sh profile_deformation_test
scripts/test.sh layered_puppet_test
scripts/test.sh layered_puppet_spec_test
scripts/test.sh puppet_document_test
scripts/test.sh puppet_document_json_test
scripts/test.sh puppet_editor_session_test
scripts/test.sh puppet_build_json_test
scripts/test.sh puppet_editor_workspace_test
scripts/test.sh puppet_editor_page_test
scripts/test.sh layered_puppet_diagnostics_test
scripts/test.sh semantic_layer_import_test
```
