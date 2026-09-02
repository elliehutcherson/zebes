# Character-binding experiment: results and open problems

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

This experiment tests whether freely generated reference art can be bound to
deterministic geometry, posed, and used as identity/style evidence without
reintroducing frame drift. Generation runs on local ComfyUI.

## Current status in one paragraph

The reference-first route now has a target-view binder. A freely generated
profile mouse is isolated exactly, thinned to a pruned medial axis, assigned
semantic head, trunk, arm, hip, knee, and foot joints, and bound to ten bones.
The neutral deformation retains 97.3% silhouette IoU at the 256px working size.
Contact, passing, and airborne proofs remain recognizably the same large-eared,
long-coated mouse at 48px and expose different poses. These are conditioning
guides, not final art: the neutral coat hides leg surfaces that no single-image
deformation can recover.

---

# Historical mannequin architecture

The original route was a pure Python parametric mannequin under the former
`experiments/mannequin/` name. It is retained below as experiment history, not
the active implementation. The active flat layout and C++ boundary are described
in `README.md`.

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
| `isolate.py` | Background separation for generated frames | Its exact mask is retained as the reference silhouette |
| `measure.py` | Proportion signatures, drift comparison | Scale-invariant: widths normalised by figure height |
| `fit.py` | Derive reusable constraints from a character image | Separates central skull from persistent head lobes |
| `comfy_client.py` | HTTP bridge to ComfyUI | No retries anywhere, by design |
| `workflow.py` | Patch API-format templates by `ZEBES_` node title | Templates are exported from the UI, never hand-written |
| `cli.py` | `render`, `report`, `gate`, `fit`, `comfy`, `template` | Fit emits isolated, silhouette, wireframe, and rig-diagnostic evidence |
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

## 7. Reference-first fitting — head solved, complete rig rejected

The free mouse image is isolated before any geometry is inferred. That exact
mask is written as the authoritative silhouette. The fitter then finds the
central skull separately from paired upper lobes, so the large ears become two
head-parented ellipsoids rather than inflating the skull into one circle.

Measured on the original freely generated mouse:

| Evidence | Result |
|---|---:|
| Exact isolated silhouette | retained byte-for-byte as the mask |
| Fitted head silhouette IoU | **90.9%** |
| Fitted complete-rig silhouette IoU | **68.8%** |
| Publish threshold | 85% head, 80% complete rig |

The fit correctly rejects. Visual evidence shows that the remaining error is
not the ears: it is the satchel, tail, long coat, cast shadow, and oversized
carried shoulder/limb radii. No constraint file is published from a rejected
fit. A second unconstrained prompt produced a cleaner profile mouse without a
bag or broad shadow, but the current fitter is explicitly front-view and does
not pretend that profile measurements determine unseen depth.

## 8. Profile silhouette binding — first posed proof

`bind-profile` uses the isolated silhouette rather than the rejected primitive
rig outline. Zhang-Suen thinning produces a one-pixel medial axis; short terminal
branches are pruned; graph paths locate the two feet, visible leg split, trunk,
head interior, and lateral arm evidence. The anatomical hip is placed above the
visible split because the long coat hides it.

Measured on the selected unconstrained profile reference:

| Evidence | Result |
|---|---:|
| Working mask | 256 × 256 from the 1024px isolated source |
| Medial-axis pixels | 505 |
| Semantic joints / bones | 13 / 10 |
| Neutral posed-mask IoU | **97.3%** |
| Proof poses | neutral, contact, passing, airborne |
| Recognizable preview | isolated colors warped through the same ten bones |

Visual review includes the mouse artwork, not only masks. The original, neutral,
contact, passing, and airborne color previews retain the same face, large ears,
green hood and coat, red scarf, belt, and boots. The contact and airborne
previews also expose the remaining deformation errors plainly: coat sections
stretch around moving arms, and boots cannot supply the hidden leg pixels absent
from the neutral reference. These previews are diagnostic inputs to the next
four-image generation gate, not proposed animation frames.

## 9. Deterministic C++ silhouette core — ported and fuzzed

The stable boundary now lives in `src/artwork/profile_silhouette`. It consumes
production `IsolateSubject` output, reduces the exact alpha mask, performs
Zhang-Suen thinning, prunes short branches, reports topology, and renders neutral
and posed binary controls. The active experiment was renamed and flattened to
`experiments/character_binding`; rejected mannequin geometry code was removed.
Python retains only unsettled semantic-joint/deformation policy and ComfyUI
orchestration, neither of which is an engine dependency.

Three independently generated mouse identities exercised different failure
classes rather than competing for final-art selection:

| Identity | C++ isolation/topology result | What the step tells us |
|---|---|---|
| seed 11 | Rejected: competing components of 115,873 and 29,643 pixels | Its background/shadow is not bindable evidence |
| seed 23 | Accepted at explicit RGB matte distance 128: 18,974 silhouette pixels, 573 axis pixels, 2 components | Main candidate; also exposes one disconnected topology component |
| seed 45 | Accepted at distance 128: 4,700 silhouette pixels, 218 axis pixels, 1 component | The same extractor handles a much smaller/chunkier identity |

The fuzz set found a real source contract: generated near-white backgrounds vary
too much for the production isolation default of 36. Future reference prompts
should request transparency or a truly flat matte; widening to 128 must remain
an explicit experiment parameter. The C++ boundary also renders a binary neutral
control—exact outer contour plus medial axis, white on black—using the same
contract as later posed semantic controls.

## 10. Four-pose identity generation — identity pass, pose fail

The bounded runner sends exactly neutral, contact, passing, and airborne guides,
uses one uploaded identity, records every digest and setting, and makes no cycle
claim.

The first two runs mistakenly sent a filled diagnostic wireframe to a
Canny-trained model. Strength 0.5 retained identity but returned four standing
poses. Strength 0.9 added small limb differences, while airborne still stood on
the ground.

The third run corrected the channel: each input is a binary outer contour plus
thick semantic bones and joint dots. Neutral, contact, and passing remain
left-facing and show clearer leg/arm differences; identity is again stable.
Airborne still stands on the ground, spreads its arms, and turns toward the
camera. Correct Canny input improves local articulation but does not convey
contact state, elevation, or front/back limb order. Canny is therefore rejected
as the sole pose-control channel.

## 11. Ordinal-depth-only gate — same control conflict

The binder now assigns experiment-owned front/rear policy to each semantic bone,
writes 1-based posed layer maps, and records per-pose grayscale values. Stable
C++ validates and renders the ordinal-depth pixels.

Two bounded depth-only runs reused the same identity and four poses:

| Depth strength/end | Identity/style | Pose/elevation | Verdict |
|---|---|---|---|
| 0.45 / 0.70 | Strong: recognizable face, ears, coat, scarf, belt, boots | Four upright standing variations; airborne grounded | Reject pose |
| 0.90 / 0.95 | Weak: face, facing, coat construction, and pixel style drift badly | Larger limb differences; airborne finally floats | Reject identity/style |

Depth carries elevation and ordering only when held strongly enough to overwrite
the identity/style signal. Weak depth preserves the mouse but is ignored. This
reproduces the original one-knob/two-directions failure on cleaner reference-first
inputs. Depth is rejected as the sole control channel.

## 12. Dual semantic-edge plus weak-depth gate — closed

The final workflow was built in the ComfyUI graph editor and exported in API
format. It chained semantic Canny at strength/end 0.90/0.95 with ordinal depth
at 0.25/0.70, plus the same IP-Adapter identity and four locked poses.

Identity and pixel style remain strong, and contact/passing show modest local
leg differences. Airborne still stands on the ground and turns toward the
camera. Weak depth adds no usable elevation or front/back ordering when Canny
and identity dominate.

This triggers the pre-registered stop rule. Canny-only, depth-only at weak and
strong settings, and dual Canny/depth have all failed the four-pose articulation
gate. Diffusion-based animation control is closed; no complete cycle or further
weight sweep is justified.

## 13. Direct C++ deformation — renderer pass, source-layer fail

`profile_deformation` performs inverse sampling rather than forward cut-and-paste:
each target pixel maps through its bone into the original source, blends incident
bone transforms near joints, and may sample only artwork owned by the same layer.

| Proof | Mapped / unmapped | Visual result |
|---|---:|---|
| Neutral | 18,974 / 0 | Pixel-exact reconstruction; zero differing pixels |
| Contact | 18,852 / 0 | Same recognizable mouse and no sampling holes, but boots/coat still intersect |

The C++ renderer solves deterministic mapping and joint continuity. It cannot
solve bad semantic ownership in the target layer map or invent upper-leg pixels
hidden by the neutral coat. The two-pose gate therefore rejects the current
automatically inferred source layers, not the renderer.

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

**Complete fitting is still inaccurate.** The skull and large ears now match,
but clothing silhouettes, carried props, tails, and limb thickness are not yet
recovered as poseable parts. The exact isolated silhouette is retained so this
loss is visible rather than hidden.

**Limb radii and all depths are carried from a base preset**, not measured. The
wireframe and rig diagnostic expose those carried values, and the IoU gate
prevents them from becoming accepted constraints when they visibly disagree.

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

Committed historical verdicts:
`evidence/run-01-verdict.json` and `evidence/run-02-verdict.json`.
Current renders are gitignored and regenerable.

Tests: `tests/character_binding_test.py`,
`tests/character_binding_comfy_test.py`, and
`tests/artwork/profile_silhouette_test.cc`. The ComfyUI tests run against an
in-process stub, so they pass with `derry` switched off.

---

# What I would do next, in order

1. **Keep fuzzing identity acquisition.** Generate varied mice with different
   proportions and clothes, but require transparent or truly flat backgrounds
   and separated limbs. The goal is algorithm robustness, not identity selection.

2. **Port stable deterministic stages to C++.** Isolation and medial-axis
   extraction are complete. Port semantic joint inference only after the fuzz
   set settles its rules; then replace hard pixel ownership with smooth mesh
   weights and explicit front/back limb layers in platform-neutral C++.

3. **Acquire separated source layers.** Direct C++ inverse deformation is now
   pixel-exact in neutral and complete in contact, but the contact image still
   places boot artwork through the coat because the neutral source hides the
   upper legs. Generate or author one bind-pose reference with visibly separated
   limbs, or fixed supplemental rear/front limb patches. Re-run only neutral and
   contact before considering a complete locomotion sequence.
