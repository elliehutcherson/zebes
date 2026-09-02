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
control. The next experiment is direct C++ layered mesh deformation: render
neutral and contact from the isolated reference without regenerating either
frame.

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
