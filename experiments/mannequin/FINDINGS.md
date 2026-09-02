# Mannequin experiment: architecture, results, and open problems

Written 2026-09-01 as a handoff. Everything below is measured unless it says
otherwise. Where a claim is a guess, it says so.

## What this experiment is for

Generated sprite frames drift. `docs/history/animation-pose-conditioned-experiment.md`
records six provider turns across four pilots, all rejected: frame 0 came back
"visibly chunkier and more stout", frame 6 "leaner", with helmet size and waist
width differing between frames. Pose obedience was fine. Body mass was not.

Two structural causes, neither fixable by prompting:

1. **Frames were generated independently.** Nothing tied frame 6 to frame 0
   except a reference image, which an image API treats as a suggestion.
2. **There was no hard conditioning channel.** The OpenAI and Codex image APIs
   accept reference images, not control maps.

This experiment tests whether a parametric 3D figure, rendered to conditioning
maps, can supply the missing constraint. It makes no provider calls; generation
runs on local ComfyUI.

## Current status in one paragraph

The consistency problem is solved and the style problem is mostly solved, but
never at the same time in a measured run. Depth conditioning holds proportions
to 1.3% across eleven of twelve frames but destroys the art style. Edge
conditioning preserves the style and transfers poses but has not been measured
across twelve frames. The rig-fitting step, which was meant to join them, is
built but fits badly: 50% too narrow overall, per-band errors from −35% to +57%.

---

# Architecture

Pure Python 3.14 standard library. No numpy, no Pillow, no Blender. Runs from
`experiments/mannequin/`.

## Data flow

```
measurements.Proportions        a character, in head units
        │
        ├── costume.Costume     helmet / ears / tail / belt / cape / weapon
        ▼
skeleton.build_rig              joints + volume shells (capsules, ellipsoids)
        │
        ├── pose.Pose           joint rotations; cycles interpolated from keys
        ▼
skeleton.solve                  forward kinematics → world transforms
skeleton.seat_on_ground         seat on the planted foot; airborne frames float
        │
        ▼
project.project                 orthographic → screen primitives, per view
        │
        ▼
raster.rasterize                depth-buffered: coverage + depth + region
        │
        ├── raster.write_depth / silhouette / outline / regions
        ├── openpose.write_skeleton_png + to_json
        └── render_svg.render   construction / silhouette / lineart
        │
        ▼
comfy_client + workflow         upload maps, patch a template, queue, download
        │
        ▼
isolate.subject_mask            separate the generated figure from background
        │
        ▼
measure.signature / compare     scale-invariant drift gate
```

## Modules

| Module | Owns | Notes |
|---|---|---|
| `math3d.py` | 4×4 transforms, vectors | Euler XYZ; no quaternions |
| `measurements.py` | `Proportions` in head units, presets, resolution tiers | Vertical closure is validated, not assumed |
| `skeleton.py` | Joints, `Capsule`/`Ellipsoid` shells, FK, seating | Elliptical cross-sections; see limitation below |
| `costume.py` | Helmet, pauldrons, belt, backpack, cape, weapon, ears, tail | Sized in head units so a kit fits any figure |
| `pose.py` | Pose library, mirroring, cycle interpolation | 12-frame run from 4 keys; second half mirrors the first |
| `project.py` | Orthographic projection, six named views, `Layout` | Everything in pixels; see the units bug below |
| `raster.py` | Depth-buffered rasteriser, map writers, clip detection | One pass fills coverage + depth + region together |
| `render_svg.py` | Vector output in three modes | Artist-facing; no C++ analogue intended |
| `openpose.py` | COCO-18 export, canonical coloured skeleton | Colours match the reference renderer |
| `png.py` | Minimal PNG writer, narrow reader | Superseded by `src/common/image_io.h` at port time |
| `isolate.py` | Background separation for generated frames | Experiment-side stand-in for `src/artwork/isolate_subject.h` |
| `measure.py` | Proportion signatures, drift comparison | Scale-invariant: widths normalised by figure height |
| `fit.py` | Derive `Proportions` from a character image | Built, not yet accurate |
| `comfy_client.py` | HTTP bridge to ComfyUI | No retries anywhere, by design |
| `workflow.py` | Patch API-format templates by `ZEBES_` node title | Templates are exported from the UI, never hand-written |
| `cli.py` | `render`, `report`, `gate`, `fit`, `comfy`, `template` | |
| `spike.py` | The 12-frame experiment with pre-registered criteria | Thresholds fixed in code before a run |
| `tunnel.sh` | Ensures the SSH port-forward and restarts ComfyUI | The forward dies on idle |

## Design decisions worth keeping

**The rig is 3D, not per-view 2D.** A per-view system cannot guarantee the side
profile describes the same body as the front. Every view is a projection of one
solved skeleton, so cross-view consistency is structural.

**Scale and contact line come from the measurement set, never from a frame's
bounding box.** Every frame in a set therefore shares one origin and one ground
line. A pose is seated on the planted foot it names; airborne frames
(`planted_foot is None`) are deliberately not seated.

**Cross-sections are elliptical.** `rx` across the body and `rz` front-to-back
are separate, which is what makes a side profile narrower than a front view
rather than the same blob rotated.

**Renders that would run off the canvas fail** rather than cropping. Scale is
fixed, so a wide pose needs a wider canvas, not a smaller figure.

**Templates are exported from the ComfyUI UI, not hand-authored**, and patched
by node *title* rather than numeric id, because ids shift whenever the graph is
edited.

## Infrastructure

ComfyUI 0.34.0 on `derry` (RTX 3090, 24GB, Linux Mint 22.3, torch 2.13.0+cu130).
It binds loopback only and is reached over an SSH tunnel — **do not open port
8188**, ComfyUI has no authentication. `./tunnel.sh` ensures the forward.

Models on derry: SDXL base 1.0, xinsir depth ControlNet, xinsir canny ControlNet,
IP-Adapter plus SDXL, CLIP-ViT-H vision encoder, `pixel-art-xl` LoRA.

---

# What was tried, in order

## 1. Depth conditioning, prompt-only identity — failed

Twelve-frame run cycle, fixed seed, pre-registered 10% head-width drift limit.

| Run | ControlNet | Head drift | Art |
|---|---|---|---|
| run-01 | strength 0.55, end 0.50 | **337%** | good — armour, cape, helmet |
| run-02 | strength 0.90, end 0.95 | **33.8%** | collapses to a grey mannequin |

**One knob, two directions.** Registration needs the control held to the end of
sampling; prompt-supplied identity needs it released. Conclusion: identity
cannot come from the prompt.

## 2. Depth conditioning plus IP-Adapter — best consistency measured

run-03, strength 0.90, IP-Adapter weight 0.7 from a generated reference.

Eleven of twelve frames measured 153–156px head width against a guide of
154–155px: **1.3% drift against a 10% limit**. Frame 0 was an outlier at 245px,
which pushed the reported figure to 58.1% and the run to REJECTED.

The failure was diagnostic. Ten frames rendered a plain white egg for a head and
two rendered a red hood — and the head is the one part of the guide with no real
geometry. **Everywhere the guide had shape, output was consistent to 1.3%.**

## 3. Judging at sprite size — the thing that was being optimised was wrong

Everything above was judged at 1024px. The sprite ships at 48px. Downsampled,
the painterly SDXL output is unreadable mush regardless of consistency.

## 4. Pixel-art LoRA plus chunky proportions — style becomes viable

Swapping SDXL base for the same model with `pixel-art-xl`, at three-head
proportions, produced a front view that reads as a game sprite at 48px: face,
tunic, scarf, hands, feet all legible. Side views still failed.

## 5. Edge conditioning instead of depth — the style unlock

A depth map says "solid 3D object with volume", so the model renders something
shaded and volumetric that turns to mush when shrunk. An edge map only says
where the outline runs.

Swapping the depth ControlNet for canny, keeping the LoRA and IP-Adapter:

- The character survives control at 0.0, 0.3, 0.5 and 0.7.
- **Side views read clearly at 48px**, where depth-conditioned side views were
  unreadable.
- Poses transfer: contact and passing frames are visibly different and correct.

Not measured: twelve-frame consistency through this path.

## 6. Unconstrained generation — the style ceiling

With control disabled entirely, the pixel-art LoRA produces genuinely good
characters: cute mice with round ears, expressive eyes, scarves and satchels,
readable at 48px. This is the quality bar, and it is what the rig must stop
destroying.

## 7. Fitting the rig to a generated character — built, inaccurate

Fits `Proportions` from a front-facing standing figure using two landmarks: the
neck (narrowest row in the upper half) and the crotch (first row where coverage
splits into two runs). Vertical closure holds by construction.

Measured against the mouse it was fitted to:

| | Mouse | Rig | Error |
|---|---|---|---|
| Height | 856px | 770px | −10% |
| Width | 761px | 378px | **−50%** |
| Per-band width | | | **−35% to +57%** |

**This is not good enough to use.** It gets the vertical landmarks roughly right
and the widths badly wrong.

---

# The one rule that explains most failures

**A feature exists in the output only if it exists in the guide.** Demonstrated
five times:

| Missing from the guide | What the generator produced |
|---|---|
| Sword | A malformed double-bladed weapon |
| Face | A featureless bag, differing between frames |
| Hands | Clubs; an arm that turned into a blade |
| Ears | A hood instead |
| Facing direction on a symmetric head | A three-quarter *front* view rendered as a back view |

Adding the geometry fixed each one. This is the most reliable predictor found.

---

# Known bugs and limitations

**Fitting is inaccurate.** See above. The head width uses a median across the
head band, which lands between skull and ear span and is right for neither. Ears
are not modelled at all, which is most of the 50% width error.

**Limb radii and all depths are carried from a base preset**, not measured. They
are reported as carried, but they are wrong for any character whose limbs differ
from the base.

**Capsule geometry cannot express a grip.** A weapon sits beside the fist because
fingers wrapping a shaft are not expressible. This is a ceiling, not a bug.

**Ellipsoid axes are world-aligned.** Only an ellipsoid's centre follows its
joint, so an elongated attachment cannot be angled. Hands are spheres for this
reason. Anything needing orientation must be a capsule between two joints.

**Side views of an A-pose are ambiguous.** Arms overlap the torso and legs
overlap each other, so the guide carries little information. Run poses separate
the limbs and do not have this problem.

**The SSH tunnel dies on idle.** `./tunnel.sh` before any run.

**Isolation is a stand-in.** It floods background inward by colour distance from
the sampled border. It fails on a figure touching the frame edge, and it counted
a two-pixel sliver as part of a figure until a noise floor was added.

## Instrument bugs — read this before trusting a measurement

Three of the first four "failures" were the measuring tool, not the generator.
All were caught the same way: **the guides come from one rig at fixed scale, so
their measured head width is identical by construction. Any drift measured on
the guides is a bug in the instrument.** Score the guides as a control.

Found this way: the head band derived from the silhouette's top (a raised sword
is the topmost pixel); width measured leftmost-to-rightmost (folding the gap
between sword and body into the body); and isolation by luminance (a red cape is
~60 luminance, so the flood poured through it and shattered the mask).

A fourth, found by a test: sampling background colour from four corners breaks
when the subject covers two of them. It samples the whole border ring now.

---

# Evidence

Committed: `out/spike/run-01/verdict.json`, `out/spike/run-02/verdict.json`.
Renders are gitignored and regenerable.

`out/review/` holds a numbered walkthrough, from the 48px mush through the
edge-control results to the fitted overlay.

Tests: `tests/mannequin_test.py` and `tests/mannequin_comfy_test.py` at the
repository root, run by `scripts/build_and_test.sh`. The ComfyUI tests run
against an in-process stub, so they pass with derry switched off.

---

# What I would do next, in order

1. **Measure the edge-conditioned path across twelve frames at 48px.** This is
   the only untested combination that has looked right, and it is one `spike.py`
   run with `--workflow workflows/pixelart-canny-ipadapter.json`. Everything
   else is speculation until this number exists.

2. **Fix or abandon fitting.** The width error is the blocker. Two options:
   model ears and hats as separate volumes so the head band measures a skull, or
   drop automatic fitting and let a human enter four numbers against the overlay.
   The overlay is the valuable part and it already works.

3. **Judge everything at 48px from now on.** Nothing above 1024px predicted
   anything about the shipped size.

4. **Decide on the 3D route.** Dead Cells rendered characters 50px tall directly
   from 3D with no anti-aliasing, and never had a high-resolution image to lose
   detail from. That is why their sprites are crisp and downsampled diffusion
   output is soft. If crispness matters, this is the route: TRELLIS 2 produces a
   mesh from one image in ~30s on a 3090, and headless Blender can rig and pose
   it from the joint positions this rig already computes. Untested.
