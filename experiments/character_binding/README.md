# Character-binding experiment

Reference-first character animation research. Generated identities are fuzz inputs,
not production-art candidates: varied ears, clothes, proportions, poses, scale,
and backgrounds expose assumptions in isolation, topology, binding, and control.

Generated animation remains outside the production roadmap. This experiment may
produce evidence; it does not delay the imported/manual frame-set pipeline.

## Stage goals and owners

| Stage | Goal | Owner |
|---|---|---|
| Generate varied references | Fuzz the algorithms across different identities | Local ComfyUI orchestration in Python |
| Isolate subject | Preserve the exact character boundary and reject ambiguous backgrounds | C++ `IsolateSubject` |
| Extract topology | Produce a deterministic medial axis and topology diagnostics | C++ `profile_silhouette` |
| Render neutral edge control | Emit exact contour plus medial axis as binary Canny input | C++ `profile_silhouette` |
| Infer semantic joints | Explore head, trunk, arm, hip, knee, and foot rules | Python prototype; not stable enough to port |
| Deform and pose | Expose ownership, hidden-surface, and layering failures | Python diagnostic prototype; known-bad hard binding |
| Four-pose generation gate | Test identity retention separately from pose obedience | Python ComfyUI runner |
| Full animation | Test timing, registration, loop, palette, and live playback | Existing C++ frame-set pipeline, only after the gate passes |

Python is not an engine dependency. Stable pixel processing moves to C++; failed
or unsettled algorithms stay disposable until their observable rules survive the
identity fuzz set.

## Layout

The experiment is one flat Python package rather than a package nested inside a
same-named directory:

```text
experiments/character_binding/
  cli.py                thin command-line orchestration
  comfy_client.py       local ComfyUI HTTP bridge
  profile_bind.py       experimental semantic binding/deformation
  profile_proof.py      bounded four-pose generation runner
  workflow.py           exported workflow patching
  png.py                experiment-only preview PNG I/O
  render_mouse_production.py
                        authored 48px mouse player clip renderer
  render_character_family.py
                        data-driven biped/quadruped/flyer renderer
  character_specs/     one JSON specimen per reusable family member
  workflows/            exported ComfyUI API templates
  evidence/             committed historical verdicts
  README.md
  FINDINGS.md
```

Stable C++ implementation:

```text
src/artwork/profile_silhouette.h
src/artwork/profile_silhouette.cc
src/artwork/profile_deformation.h
src/artwork/profile_deformation.cc
scripts/extract_profile_silhouette.cc
scripts/render_profile_pose_control.cc
scripts/render_profile_pose_depth.cc
scripts/render_profile_deformation.cc
tests/artwork/profile_silhouette_test.cc
tests/artwork/profile_deformation_test.cc
```

## Build the C++ proof tools

```bash
cmake --preset dev
cmake --build build/dev --target extract_profile_silhouette \
  render_profile_pose_control render_profile_pose_depth \
  render_profile_deformation
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

## 2. Prototype semantic binding

The prototype consumes the C++-isolated PNG and C++ skeleton evidence. Python no
longer duplicates background isolation, mask reduction, thinning, branch
pruning, binary posed-control rendering, or ordinal-depth pixel encoding. The
thin CLI invokes `render_profile_pose_control` and `render_profile_pose_depth`
after experimental joint inference and front/rear layer policy.

```bash
PYTHONPATH=experiments python3 -m character_binding.cli bind-profile \
  experiments/character_binding/out/profile/isolated.png \
  experiments/character_binding/out/profile/skeleton.png \
  --out experiments/character_binding/out/profile-binding
```

Important outputs:

- `skeleton.png`: semantic joints over the isolated silhouette;
- `binding-regions.png`: current hard pixel-to-bone ownership;
- `pose-*-layers.png`: experimental 1-based front/rear bone ownership;
- `pose-*-control.png`: C++ binary contour plus semantic bones and joints;
- `pose-*-depth.png`: C++ ordinal grayscale depth;
- `pose-*-color.png`: recognizable diagnostics showing deformation failures;
- `binding.json`: reproducible joints, bones, poses, and depth policy.

The color previews are not proposed frames. They expose known issues: coat/hip
ownership, hidden legs, boot intersections, hard cut-and-paste boundaries, and
missing front/back layers.

## 3. Run the bounded four-pose gate

```bash
cd experiments/character_binding
./tunnel.sh
cd ../..

PYTHONPATH=experiments python3 -m character_binding.cli generate-profile-proof \
  --binding experiments/character_binding/out/profile-binding \
  --identity profile-reference.png \
  --workflow experiments/character_binding/workflows/pixelart-canny-ipadapter.json \
  --prompt 'the same character, following the supplied pose guide exactly' \
  --control-strength 0.9 \
  --control-end-percent 0.95 \
  --out experiments/character_binding/out/profile-proof
```

This asks exactly four questions: neutral, contact, passing, and airborne. It is
an identity/pose gate, not an animation-cycle claim.

Current result: IP-Adapter retains a recognizable identity; Canny-only controls
improve local limb differences but fail airborne elevation, facing, and reliable
front/back limb order. The next model experiment needs a depth-bearing control
channel rather than more Canny tuning.

## 4. Ordinal-depth-only gate

The same runner can select C++ depth guides and the exported depth/IP-Adapter
workflow:

```bash
PYTHONPATH=experiments python3 -m character_binding.cli generate-profile-proof \
  --binding experiments/character_binding/out/profile-binding \
  --identity profile-reference.png \
  --workflow experiments/character_binding/workflows/depth-controlnet-ipadapter.json \
  --guide-kind depth \
  --prompt 'the same character, following the supplied pose and elevation exactly' \
  --control-strength 0.45 \
  --control-end-percent 0.70 \
  --out experiments/character_binding/out/profile-depth-proof
```

Weak depth preserves identity but returns four standing poses. Strong depth
finally raises airborne and changes limb order, but destroys face, facing, coat
construction, and pixel style. Depth-only is rejected.

## 5. Final dual-control gate

The dual workflow was assembled in the ComfyUI editor and exported as API JSON:

```bash
PYTHONPATH=experiments python3 -m character_binding.cli generate-profile-proof \
  --binding experiments/character_binding/out/profile-binding \
  --identity profile-reference.png \
  --workflow experiments/character_binding/workflows/pixelart-canny-depth-ipadapter.json \
  --guide-kind dual \
  --prompt 'the same character, following the supplied pose and elevation exactly' \
  --control-strength 0.90 \
  --control-end-percent 0.95 \
  --depth-strength 0.25 \
  --depth-end-percent 0.70 \
  --out experiments/character_binding/out/profile-dual-proof
```

Identity and style remain recognizable, but airborne is still grounded and turns
toward the camera. The pre-registered stop rule closes diffusion-based animation
control.

## 6. Direct C++ deformation gate

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

## 7. Direct low-poly 3D gate

The initial Blender 4.0.2 spike on `derry` established that one reusable model
could preserve identity while changing pose. It used a fixed orthographic
camera, emission-only materials, one sample, a 0.01 filter, and enlarged backing
geometry for pixel-stable edges. Neutral and contact retained identical head,
ears, muzzle, hood, scarf, coat, belt, tail, and materials.

Structural and pixel-discipline verdicts passed. The art-direction verdict did
not: the face was minimal, hands were tiny, and legs read too human. That result
closed the structural gate and became the input to the production renderer
below; the obsolete two-pose proxy entry point was removed.

## 8. Generated model-sheet mapping

The generated profile was used as a native model sheet rather than projected
onto the mesh. Its 30×44 visible bounds drove a 27×45 neutral model at 75.5%
registered silhouette IoU. The comparison isolated the remaining authored work:
cheek and eye construction, coat lapels and pockets, hands, and mouse-like
boots. Keeping the pixels off the mesh preserved real hidden geometry for later
poses.

## 9. Reusable body-plan automation

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

## 10. Production mouse player

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

build/bin/import_animation_frame_sets \
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

## Other commands

```bash
PYTHONPATH=experiments python3 -m character_binding.cli comfy
PYTHONPATH=experiments python3 -m character_binding.cli template \
  experiments/character_binding/workflows/pixelart-canny-ipadapter.json
```

ComfyUI remains loopback-only on `derry`; `tunnel.sh` uses authenticated SSH.
Do not expose port 8188 to the LAN.

## Tests

```bash
scripts/test.sh profile_silhouette_test
scripts/test.sh profile_deformation_test
python3 -m unittest tests.character_binding_test tests.character_binding_comfy_test
```

The ComfyUI suite uses an in-process HTTP stub and does not require `derry`.
