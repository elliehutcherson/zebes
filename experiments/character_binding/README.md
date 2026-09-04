# Character-binding experiment

Reference-first character animation research. Generated identities are fuzz inputs,
not production-art candidates: varied ears, clothes, proportions, poses, scale,
and backgrounds expose assumptions in isolation, topology, binding, and control.

Generated animation remains outside the production roadmap. This experiment may
produce evidence; it does not delay the imported/manual frame-set pipeline.

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
  character_specs/     Blender family inputs
  puppet_specs/        explicit C++ layered-puppet inputs
  evidence/            committed historical verdicts
  README.md
  FINDINGS.md

src/artwork/layered_puppet.{h,cc}
src/artwork/layered_puppet_diagnostics.{h,cc}
src/artwork/semantic_layer_import.{h,cc}
scripts/render_layered_puppet.cc
tests/artwork/layered_puppet_test.cc
tests/artwork/layered_puppet_diagnostics_test.cc
tests/artwork/semantic_layer_import_test.cc
```

See-through was evaluated from an isolated temporary checkout on `derry`.
Accepted source-of-truth inputs are retained under `out/`; derived renders,
virtual environments, and model caches are not repository dependencies.

## Build the C++ proof tools

```bash
cmake --preset dev
cmake --build build/dev --target extract_profile_silhouette \
  render_profile_pose_control render_profile_pose_depth \
  render_profile_deformation render_layered_puppet
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
  --source=experiments/character_binding/out/profile-binding-deformation-v2/source-color.png \
  --spec=experiments/character_binding/puppet_specs/mouse_profile_v1.json \
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
  --source=experiments/character_binding/out/profile-binding-deformation-v2/source-color.png \
  --spec=experiments/character_binding/puppet_specs/mouse_immutable_coat_v1.json \
  --semantic_root=experiments/character_binding/out/see-through-v1/optimized \
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

## Tests

```bash
scripts/test.sh profile_silhouette_test
scripts/test.sh profile_deformation_test
scripts/test.sh layered_puppet_test
scripts/test.sh layered_puppet_diagnostics_test
scripts/test.sh semantic_layer_import_test
```
