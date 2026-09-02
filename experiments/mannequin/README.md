# Parametric mannequin

A measurement-driven figure that renders to the conditioning maps an image
generator needs in order to hold one character's proportions across a set of
frames. Pure Python 3.14 standard library: no numpy, no Pillow, no Blender.

**Read [FINDINGS.md](FINDINGS.md) first.** It records the architecture, every
approach tried with its measured result, the known bugs, and what to do next.
This file is usage; that file is the state of the work.

## Why this exists

`docs/history/animation-pose-conditioned-experiment.md` records six provider
turns across four pilots, all rejected. Frame 0 came back "visibly chunkier and
more stout", frame 6 "leaner", with helmet size and waist width differing. Pose
obedience was fine. Body mass was not.

Two structural reasons, neither fixable by prompting:

1. **Frames were generated independently.** Nothing tied frame 6 to frame 0
   except a reference image, which an image API treats as a suggestion.
2. **There was no hard conditioning channel.** The OpenAI and Codex image APIs
   accept reference images, not control maps. Nothing forces a pixel to land
   where a guide says it should.

This tool supplies the missing guide, and — just as importantly — the gate that
measures whether it was obeyed.

## Scope

This experiment makes **no provider calls** and builds nothing into the engine.
`docs/handoff.md` closes generated animation inside Track 5; the deprecated
experiment routes any future articulated-guide work to "a new design and budget
outside this deprecated plan". This is that separate design.

## What it produces

Everything below comes from one solved 3D skeleton, so the views cannot disagree
about what body they describe. A per-view 2D drawing system would reintroduce
exactly the drift it is meant to prevent.

| Output | Use |
|---|---|
| `depth/` | The primary conditioning map. Required, not optional — see below |
| `silhouette/`, `outline/` | Shape and line-art conditioning |
| `regions/` | Flat per-part colours: which band is torso, which is cape |
| `openpose/` | COCO-18 keypoints as JSON and the canonical coloured skeleton |
| `svg/construction/` | The artist's mannequin: visible volumes, centrelines, head-unit grid |
| `svg/silhouette/`, `svg/lineart/` | Vector forms of the same shapes |
| `manifest.json` | Per-frame pose label, planted foot, shared origin, contact line, proportion signature |

## Depth is required, not optional

The two halves of a side-view run cycle are **pixel-identical in silhouette**.
The same two leg positions, swapped in depth. Measured on the shipped run cycle:
frames 0 and 6 differ in zero silhouette pixels and in 10,511 depth pixels.

Conditioning a generator on silhouette or line art alone therefore hands frames
0 and 6 the same guidance, and the model has no way to know which leg is in
front. That is the "reads as nearly the same pose with one arm moved rather than
opposing contact phases" failure in the deprecated experiment record.
`tests/mannequin_test.py` locks this behaviour in.

## Usage

```bash
cd experiments/mannequin

# Which measurements survive at the shipped 44 px sprite height
python3 -m mannequin.cli report --preset heroic-6h --height-px 44

# The canonical A-pose identity reference, all views
python3 -m mannequin.cli render --preset heroic-6h --costume knight \
    --pose a-pose --views front,right,back,three-quarter-right --out out/identity

# A 12-frame run cycle's conditioning maps
python3 -m mannequin.cli render --preset heroic-6h --cycle run --frames 12 \
    --views right --out out/run-right

# Drift between two isolated frames
python3 -m mannequin.cli gate reference.png candidate.png --tolerance 0.08

# Bind measurements and an exact isolated silhouette to a freely generated
# front-facing standing reference.
python3 -m mannequin.cli fit reference.png --base trickster-3h \
    --name mouse --out out/fitted

# Render posed guides from those retained constraints.
python3 -m mannequin.cli render \
    --constraints out/fitted/mouse-constraints.json \
    --cycle run --frames 12 --views right --out out/mouse-run

# For a target-view profile, derive a medial-axis skeleton directly from the
# exact isolated silhouette and emit neutral/contact/passing/airborne guides.
python3 -m mannequin.cli bind-profile profile-reference.png \
    --out out/profile-binding

# Run the deterministic C++ isolation/thinning boundary against any generated
# identity. The broad matte tolerance is explicit evidence, not a hidden default.
build/dev/bin/profile_silhouette_spike \
    --input=profile-reference.png \
    --output=out/cpp-profile/skeleton.png \
    --isolated_output=out/cpp-profile/isolated.png \
    --background_distance=128

# Ask ComfyUI only four questions: can one identity survive neutral, contact,
# passing, and airborne guides? This is not an animation-cycle claim.
python3 -m mannequin.cli generate-profile-proof \
    --binding out/profile-binding \
    --identity profile-reference.png \
    --workflow workflows/pixelart-canny-ipadapter.json \
    --prompt 'the same character, following the supplied pose guide' \
    --out out/profile-proof
```

A 12-frame set at 1024x1024 takes about 14 seconds.

`fit` writes four distinct artifacts. `*-isolated.png` retains the subject pixels
selected by the existing isolation algorithm. `*-silhouette.png` is that exact
mask in white on black; it is the authoritative reference silhouette and is
never reconstructed from circles or capsules. `*-wireframe.png` overlays the
fitted joints and rig outline for visual review. `*-rig-diagnostic.png` shows
where the poseable volume approximation misses the isolated silhouette. A low
rig IoU blocks a posed generation run; it does not invalidate the isolated mask.

`bind-profile` does not reuse the generic capsule outline. It thins the isolated
profile to a medial axis, prunes short branches, infers head, trunk, arm, hip,
knee, and foot joints, and binds both silhouette pixels and isolated source
colors to those bones. Its evidence includes the recognizable source mouse,
four warped color previews, the raw medial axis, semantic wireframe, bone
regions, posed masks, posed wireframe overlays, and a JSON manifest. The color
previews make deformation errors visible; they are not proposed final frames.
A single neutral image still cannot reveal limb surfaces hidden by a long coat.
The four-pose runner uploads `pose-*-control.png`, not the color preview or
diagnostic wireframe. Each Canny input is opaque black with a white outer contour,
semantic bones, and joint dots. This tests whether an edge model can obey the
articulation contract separately from whether IP-Adapter retains identity.

## Experiment stages and implementation boundary

| Step | Goal | Current owner |
|---|---|---|
| Generate varied references | Fuzz the binder across silhouettes, clothes, proportions, and poses; not select production identity | Python orchestration + local ComfyUI |
| Isolate the subject | Preserve the exact generated boundary and reject unusable backgrounds or competing subjects | Existing C++ `IsolateSubject` |
| Extract the medial axis | Test whether one deterministic topology algorithm survives those varied identities | C++ `profile_silhouette`; Python remains comparison evidence |
| Infer semantic joints | Turn topology into head, trunk, arm, hip, knee, and foot landmarks | Python prototype until the rules survive the fuzz set |
| Deform mask and colors | Expose intersections, hidden-surface gaps, and bad ownership before another model call | Python diagnostic prototype; final mesh/weights belong in C++ |
| Four-pose generation gate | Test whether structure plus the source image yields consistent identity and plausible hidden surfaces | Python/ComfyUI experiment only |
| Full animation and engine import | Prove native-size motion, registration, palette, loop, and live playback | Existing C++ frame-set and curation pipeline, only after the gate passes |

Python is disposable orchestration and algorithm exploration. Deterministic
pixel processing graduates to platform-neutral C++ only after its contract is
measured. No experimental generator or Python runtime becomes an engine
dependency.

## The ComfyUI box

Generation runs on the RTX 3090 machine over the LAN. That is our own hardware,
not a third-party provider: no account, no per-image cost, and no character art
leaving the house.

The box is `derry` at `192.168.1.100`. As built and verified on 2026-08-31:

| | |
|---|---|
| OS | Linux Mint 22.3, kernel 7.0.0-30 |
| GPU | RTX 3090, 24 GB, driver 595.84 |
| CPU / RAM | 24 cores, 62 GB |
| Python | 3.12.3 |
| Torch | 2.13.0+cu130, `cuda.is_available()` true, capability (8, 6) |
| ComfyUI | 0.34.0, in `~/ComfyUI` with its own venv |

CUDA 13 dropped Maxwell through Volta but still supports Ampere, so the cu130
wheels are correct for this card. `nvcc` is deliberately absent: the wheels
bundle their own CUDA runtime, and the full toolkit is only needed by custom
nodes that compile CUDA from source.

```bash
sudo apt install -y git python3-venv python3-dev build-essential libgl1
git clone --depth 1 https://github.com/comfyanonymous/ComfyUI && cd ComfyUI
python3 -m venv venv && . venv/bin/activate
pip install torch torchvision torchaudio
pip install -r requirements.txt
```

### Reach it over an SSH tunnel, not an open port

**ComfyUI has no authentication.** Anything that can reach port 8188 can queue
jobs, read every output, and read files the process can see. So the port is not
opened to the LAN at all. ComfyUI binds loopback, and the Mac reaches it through
SSH — which is already authenticated and already allowed.

On derry, ufw allows only SSH, and only from the LAN:

```bash
sudo ufw default deny incoming
sudo ufw allow from 192.168.1.0/24 to any port 22 proto tcp
sudo ufw enable
```

```bash
# derry: loopback only
cd ~/ComfyUI && . venv/bin/activate
setsid nohup python main.py --listen 127.0.0.1 --port 8188 > ~/comfyui.log 2>&1 &
```

```bash
# mac: forward the port, then talk to it as if it were local
ssh -f -N -L 8188:127.0.0.1:8188 derry
export ZEBES_COMFY_URL=http://127.0.0.1:8188
python3 -m mannequin.cli comfy
```

The tunnel and the ComfyUI process are both started by hand and do not survive a
reboot of either machine. A systemd unit on derry and an autossh tunnel on the
Mac would fix that; neither is set up yet.

`~/.ssh/config` carries a `derry` entry with `ControlMaster`, so the many short
SSH calls a generation run makes reuse one connection.

### Models

| Path under `~/ComfyUI` | File |
|---|---|
| `models/checkpoints/` | `sd_xl_base_1.0.safetensors` |
| `models/controlnet/` | `xinsir-controlnet-depth-sdxl-1.0.safetensors` |

Vanilla SDXL base is deliberate for the spike: it has the best-documented
ControlNet adherence and introduces no style variable. This experiment measures
whether depth conditioning holds proportions, and a finetune would confound that
with aesthetics. Style belongs downstream, in the existing restyle pass or a
LoRA.

IP-Adapter comes later — it needs the `ComfyUI_IPAdapter_plus` custom node plus
a CLIP vision encoder. Getting a depth-conditioned image back on core nodes
first keeps a bridge problem distinguishable from an install problem.

## Workflow templates

Templates are **not** hand-written. Build the graph in the ComfyUI UI, enable dev
mode in settings, and use **Save (API Format)**. Hand-authoring the JSON means
guessing node class names and input keys, and a wrong key fails on the server
rather than here.

Give every node the runner should drive a title starting with `ZEBES_`
(right-click a node, then Title): `ZEBES_POSITIVE`, `ZEBES_NEGATIVE`,
`ZEBES_SEED`, `ZEBES_CONTROL_IMAGE`, `ZEBES_IDENTITY_IMAGE`, `ZEBES_OUTPUT`.
Nodes are addressed by title rather than by numeric id because ids shift
whenever the graph is edited, and a template patched by id silently starts
writing the seed into the wrong node.

```bash
python3 -m mannequin.cli template workflows/depth-controlnet.json
```

## Measurement tiers

Measurements are in head units, never centimetres — a sprite pipeline cares
about ratios, and only ratios survive the trip from a 1024 px generation to a
44 px atlas cell.

The set is tiered because of resolution, not preference. At the shipped 44 px
character height one head unit is 7.3 px, so inseam lands at 23.5 px and
shoulder width at 12.6 px, but wrist radius is 0.8 px and elbow radius 1.1 px.
Circumference-style measurements still shape the silhouette the generator draws
at full size, but a gate must not fail a frame over a measurement that spans
less than a pixel in the shipped asset. `measurements.GATE_TESTABLE_FIELDS` is
the tier that may be used as evidence; run `report` to see the split at any
render size.

## Registration

Scale and the contact line come from the measurement set, never from a frame's
own bounding box, so every frame in a set shares one origin and one ground line.
A pose is seated on the **planted foot it names**, and airborne frames
(`planted_foot is None`) are deliberately not seated — seating a jump would
plant it on the floor.

Because the origin is fixed, a wide pose needs a wider canvas rather than a
smaller figure. Renders that would run off the edge fail with a message saying
so instead of quietly cropping a foot.

## Layout

| Module | Owns |
|---|---|
| `math3d.py` | 4x4 transforms and vectors |
| `measurements.py` | `Proportions` in head units, closure validation, resolution tiers |
| `skeleton.py` | Joint hierarchy, volume shells, forward kinematics, seating |
| `costume.py` | Helmet, pauldrons, belt, backpack, cape, weapon volumes |
| `pose.py` | Pose library, mirroring, cycle interpolation |
| `project.py` | Orthographic projection to screen primitives |
| `raster.py` | Depth-buffered rasteriser and map writers |
| `render_svg.py` | Vector output in three modes |
| `openpose.py` | COCO-18 export and canonical skeleton render |
| `measure.py` | Scale-invariant proportion signatures and the drift gate |
| `isolate.py` | Separates a generated frame; its exact mask is retained as the reference silhouette |
| `fit.py` | Derives reusable proportions and persistent head attachments from the isolated reference |
| `profile_bind.py` | Extracts a pruned medial axis, infers profile joints, and poses the bound silhouette |
| `spike.py` | The rung-1 experiment: render, generate, score against fixed criteria |
| `png.py` | Minimal PNG writer and a narrow reader |
| `comfy_client.py` | HTTP bridge to ComfyUI: upload, queue, wait, download |
| `workflow.py` | Patching API-format templates by `ZEBES_` node title |
| `cli.py` | `render`, `report`, `gate`, `fit`, `bind-profile`, `comfy`, `template` |

## Tests

`tests/mannequin_test.py` and `tests/mannequin_comfy_test.py` at the repository
root, run by `scripts/build_and_test.sh` through its `unittest discover` step.
During development:

```bash
python3 -m unittest tests.mannequin_test tests.mannequin_comfy_test
```

The ComfyUI tests run against a stub HTTP server in-process, so the suite passes
with the 3090 box switched off. A test that needs the GPU machine awake is a
test that gets skipped, and a skipped test is not a gate.

## If this graduates

The seams map onto existing C++ homes. `src/` has no 3D math today —
`objects/vec.h` is 2D and there is no `Vec3` or `Mat4` — so the rig types would
be genuinely new. `png.py` would be deleted in favour of
`src/common/image_io.h`, projection and rasterisation would join `src/artwork/`
alongside the other deterministic `RgbaImage` producers, and the construction
view would become an ImGui overlay through `src/editor/canvas/canvas.h` in the
style of `src/editor/anchor_gizmo_renderer.h`.
