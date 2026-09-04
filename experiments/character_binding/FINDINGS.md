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

This experiment tests whether approved 2D character art can become deterministic
animation input without identity drift. Remote pose generation is closed; the
active route is explicit layered artwork rendered and validated in C++.

## Current status

The production import/runtime graph is complete, but the Blender mouse failed
the reopened art-direction gate. The active C++ pipeline now restores accepted
See-through crops, preserves original visible pixels, and drives one complete
arm through a shoulder/elbow/wrist chain with a reusable triangulated mesh.

The first isolated arm gate passed, but full-character motion exposed a static
ghost. Exclusive ownership and torso underpaint fixed that defect: all 18,974
source pixels are singly owned and neutral changes zero pixels. Human review
then rejected the remaining deformation quality.

Sections 23 and 24 measured why, and it is mostly not deformation. The elbow is fixed:
angle-blended skinning plus a trimmed mesh and a laterally widened blend band
give zero folds and a smooth bend, with neutral still pixel-exact. Retained area
is retired as a gate, because removing the folds lowered it while the picture
improved.

The layer cut is fixed too. Ownership is derived from the layer's own alpha plus
a reach limit off the bone chain, so the stuck pixels are gone and the tail no
longer swings with the arm, and the backfill is closed by stretching the coat
outward from its interior.

Correcting the shoulder joint then exposed two gates the old rig had masked: 149
orphan pixels and 4 folded triangles, neither tunable with the current knobs.
That failure is the signal.

48px review then rejected the moved arm for a reason none of the gates measure.
It reads as a wide slab lying across the chest rather than an arm, because
`source_from_semantic_reach` starts at 22 px and takes a piece of chest coat
along with the sleeve. Standing still that is invisible; rotating the arm sweeps
torso across the body. The reach was chosen while the pivot was still at the
midline, where 22 px covered a plausible sleeve.

The passing pose compounds it by sending the arm over the front, when a side-on
run swings each arm fore and aft beside the body. The four poses were authored
against the midline pivot and want re-authoring.

Next: narrow the shoulder reach until the moving layer is a sleeve rather than a
sleeve plus chest, then re-author the poses. Only after that, the second arm and
the tail — neither is what a viewer notices first.

Inference supplies candidate RGBA only. C++ owns crop restoration, original
pixel preservation, semantic acceptance, skeleton mapping, deformation,
articulation scoring, and rejection.

## Decision ledger

| # | Attempt | Result | Decision |
|---:|---|---|---|
| 1 | Weak depth, prompted identity | 337% head drift | Reject |
| 2 | Strong depth, prompted identity | 33.8% drift; grey mannequin | Reject |
| 3 | Depth plus IP-Adapter | 11/12 frames at 1.3% drift; head outlier and identity collapse | Reject guide geometry |
| 4 | Review at native 48px | Painterly output became unreadable | Native-size review required |
| 5 | Pixel-art LoRA, chunky proportions | Front view readable; profile failed | Partial |
| 6 | Canny plus LoRA/IP-Adapter | Profiles and local pose changes readable | Retain as evidence |
| 7 | Unconstrained pixel generation | Best identity and style | Reference quality bar |
| 8 | Primitive complete-rig fitting | 68.8% IoU against 80% gate | Reject |
| 9 | Profile silhouette binding | 97.3% neutral IoU; hidden limbs absent | Structural proof only |
| 10 | Deterministic C++ silhouette core | Isolation/topology pass across fuzz set | Retain |
| 11 | Four-pose Canny generation | Identity pass; elevation/order fail | Reject |
| 12 | Ordinal depth generation | Pose/style strength conflict repeated | Reject |
| 13 | Dual Canny/depth generation | Airborne still grounded | Close diffusion animation control |
| 14 | Direct C++ deformation | Exact neutral; bad ownership remains | Renderer pass, source fail |
| 15 | Direct low-poly 3D | Stable identity/pose; crude art | Structural pass only |
| 16 | Generated model sheet for 3D | 75.5% silhouette IoU | Mapping pass, art fail |
| 17 | Shared body-plan builders | Reuse/species pass; generic art | Automation boundary established |
| 18 | Authored Blender mouse | Runtime/pipeline pass; style rejected | Keep as pipeline proof |
| 19 | Explicit layered 2D puppet | Style and four-pose direction pass; rigid seams | Continue |
| 20 | See-through V3 decomposition | Arms/boots/coat pass; legs/tail/anatomy fail | Candidate generator only |
| 21 | Isolated skeleton-skinned arm | Exact part neutral and connected bend; full composite ghosts | Add ownership gate |
| 22 | Exclusive ownership and torso underpaint | 18,974 singly owned pixels; exact neutral; no ghost | Ownership pass |
| 23 | Human mesh-deformation review | Grid reads as rigid sections; ~13% area compression | Reject linear deformation |
| 24 | Measured diagnosis | 105 stuck px, 876 unbacked px, 14 folds on artwork | Fix the layer cut before the solver |
| 25 | Angle-blended skinning | Neutral exact; 88.1%/87.8%; folds unchanged | Keep; not enough alone |
| 26 | Trimmed mesh, laterally widened blend | Zero folds, exact neutral, smooth elbow; area fell to 81.9%/81.0% | Accept; retire the area gate |
| 27 | Ownership from layer alpha plus bone reach | Orphans 105 to 0, backfill 876 to 598, tail no longer swings with the arm | Accept; gate on |
| 28 | Corrected shoulder joint | Arm pivots at its socket; poses re-solved by IK, passing clamped; 149 orphans and 4 folds appear | Accept; failure is the signal |
| 29 | Backfill by stretching | Uncovered 745 to 0, contact tears 355 to 194; 47% of the region behind the arm is invented | Accept; gate on |
| 30 | 48px review of the moved arm | Reads as a slab across the chest: the layer owns chest coat, and the pose sweeps it over the front | Reject; narrow the reach and re-author the poses |

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
The rejected semantic-joint and ComfyUI animation-control Python prototypes were
removed after their stop rules fired. Stable silhouette, deformation, and
layered-puppet processing now live in C++.

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

## 14. Direct low-poly 3D — structural pass

Blender 4.0.2 was installed on `derry`. One deterministic script builds a crude
mouse from low-poly ellipsoids, prisms, and eight-sided limb segments, then
renders neutral and contact directly at 48×48. Camera, scale, model geometry,
materials, and depth order are identical between poses; only joint coordinates
change.

| Gate | Result |
|---|---|
| Same identity and proportions | Pass: head, ears, muzzle, hood, scarf, coat, belt, and tail are unchanged |
| Contact readability | Pass: wide opposing legs and arms read immediately |
| Foot/coat intersection | Pass: both legs begin below the short coat; no boot enters the torso |
| Ground contact | Pass: lead foot planted, rear foot visibly lifted |
| Native framing | Pass: both poses fit one 48×48 orthographic frame |
| Production art quality | Fail: proxy is crude, hands are small, legs read too human |
| Raw edge colors | 40 neutral / 40 contact with one Eevee sample |
| Deterministic quantization | Pass: neutral/contact reduce to 8/9 colors |

This is the first route that obeys pose structurally without changing identity.
Emission-only materials and enlarged black backing geometry produce crisp native
pixels and readable outlines. It validates direct low-resolution 3D as the
animation source, not this proxy as production art. Remaining work is model
quality: better chibi anatomy, stronger hands and face, then normal-map export.

## 15. Generated profile as native model sheet — measurable fit

The renderer now loads the C++-isolated generated profile, measures its alpha
bounds, mirrors it right, and normalizes it to the model's 5.8-unit target height.
It renders the model sheet, neutral model, and contact model through the same
48×48 orthographic camera.

| Measurement | Generated reference | Neutral model |
|---|---:|---:|
| Visible bounds | 30×44 | 27×45 |
| Registered silhouette IoU | — | **75.5%** |

The earlier proxy was only 21 pixels wide, so reference-driven ear, head, and
coat dimensions materially close the silhouette gap. Visual review still rejects
the model as production art: its face is a disk rather than the reference's
cheek/eye/brow construction, the coat lacks lapels and pockets, and hands/boots
remain generic. The mapping path is valid; more generated pixels are not needed
to identify the modeling work.

The reference is a model sheet, not a projected texture. This preserves complete
hidden geometry for later poses instead of reintroducing the flat-image problem.

## 16. Reusable body-plan families — automation pass

A data-driven Blender renderer produced seven named specimens from three shared
body-plan builders. The reviewed 7×2 matrix contains neutral and action renders
for humanoid, mouse, badger, rabbit, fox, cat, and bat. All fourteen frames
remain inside the 48×48 camera, expose the intended pose change, and retain the
same identity across poses.

The strongest evidence is reuse within the quadruped row. Rabbit, fox, cat, and
badger share all limb and pose construction while remaining distinguishable
through proportions, ears, tails, markings, and palette. The bat also shows why
body-plan boundaries matter: wings are topology, not another quadruped
parameter. The badger is the least specific specimen and needs a stronger mask,
back stripe, and shoulder mass before a production gate.

This changes the expected per-character cost. Within a known family, setup is a
validated JSON spec plus deliberate art tuning. A repeated trait should extend
the family builder once. Only a new topology requires a new builder. Unique
faces, garments, accessories, and high-quality motion cannot be automated away;
they remain the work that determines whether the final sprite reads as the
actual character rather than merely its species.

The Blender batch command rendered all seven specs successfully in one process.
This verifies schema validation, shared-builder dispatch, deterministic
48×48 output, source `.blend` capture, and manifest generation together.

## 17. Mouse production set — asset pass

The two-pose proxy was replaced by one authored Blender model that renders 32
frames across six clips: left/right idle, run, and airborne. The finished 48px
model adds the missing eye patch, cheek, muzzle, ear highlights, hood, scarf,
lapels, coat panels, pockets, cuffs, belt, larger hands, boots, and tail. The
idle clips remain grounded, run traverses eight distinct gait poses, and
airborne preserves both a transparent border and a visible ground gap.

All six source strips passed `AnimationFrameSetPipeline` validation and were
retained as imported artwork. Six recipes now own the processed Textures,
Sprites, timings, playback modes, and bindings on the stable player Blueprint.
Catacombs entity 4 was migrated from the proof Sprite to the production
idle-right Sprite without changing its 32×64 collider.

The production attempt exposed and fixed one latent lifecycle bug:
`ValidateAnimationFrameSetRecipe` incorrectly scaled texture atlas geometry when
`render_scale` exceeded one. Texture geometry now stays at native resolution;
only render dimensions and offsets scale. A regression test covers the
production value of two.

Headless Sprite review now emits contact and alignment sheets, actual foreground
bounds, adjacent differences, loop closure, and hold-final evidence in addition
to its prior native/enlarged frames and ordered strip. A repeated persisted
run-right review was byte-for-byte identical. Focused Catacombs reviews at
0.5×, 1×, and 2× load the production mouse; the 2× view preserves its face,
ears, scarf, coat, hands, and boots against the route.

The runtime has no Texture or Sprite normal-map channel, so this production
asset deliberately ships RGBA color frames only. An unconsumed normal sidecar
would not be a production feature.

## 18. Explicit layered 2D puppet — direction pass

The generated profile was split into ten authored head, torso, arm, and leg
parts. `mouse_profile_v1.json` replaces inferred semantic joints with explicit
joint positions, supplies dark hidden-leg underpaint, and declares front/rear
draw order per pose. C++ `layered_puppet` inverse-samples each rigid part
through the existing four-pose skeleton and publishes working, native 48px,
enlarged, part, digest, and manifest evidence.

The neutral silhouette retains 95.3% IoU against the generated source. Neutral,
contact, passing, and airborne produce four unique frame digests; airborne ends
four pixels above the grounded contact band. The generated face, ears, hood,
scarf, coat texture, and palette survive all four poses. A disposable-asset-root
Catacombs review at 0.5x, 1x, and 2x reports no objective findings.

This passes the direction gate: separated 2D artwork retains the target style
that the primitive Blender model lost, while explicit skeleton ownership
produces readable pose changes. It does not pass a production animation gate.
The polygon-extracted sleeves and boots still carry pixels from the flattened
coat, and rigid upper/lower pieces expose seams at large joint rotations. The
next input must be a genuinely painted part sheet with joint overlap and small
pose-specific corrective pieces. More topology inference or another whole-image
deformer is not justified.

## 19. See-through semantic decomposition — partial pass

The upstream See-through V3 model at revision
`7f139bb25c46a0c8ac720d95ddab185fcda5451c` processed the exact isolated
1024px mouse
(`97bce0d9892ace41f822f8aa3ecb33281fc09e43e51602bda76dad051a540b45`)
at 1280px, seed 42, bf16 on the 24GB RTX 3090. After stale ComfyUI
weights were unloaded, the full layer and depth pass
completed in 520.87 seconds and exported PSD, RGBA, crop, and pseudo-depth
evidence.

The useful outputs are substantial: left and right `handwear` layers reconstruct
complete sleeves and hands behind the coat; `footwear` preserves both boots; and
`topwear` preserves the hood, scarf, belt, pockets, and coat texture. These are
better hidden-surface candidates than the solid and polygon-cropped underpaint
in the first puppet proof.

The model is not a complete mouse decomposer. `bottomwear`, `legwear`, and
`tail` are empty. The ear layers become human ears, while `headwear` and `back
hair` hallucinate human anatomy. This is the expected domain gap from a
human-anime Live2D taxonomy. Original head/torso pixels must therefore remain
authoritative, and every inferred layer needs a C++ semantic and articulation
gate.

Verdict: retain See-through as an offline candidate generator for arms, boots,
and coat completion; reject automatic whole-character acceptance. Do not train
a custom parser or add graph-cut complexity yet. First map only the useful RGBA
layers into the C++ puppet, split footwear by connected component, and obtain
legs and tail through targeted completion or authored corrections. The external
PyTorch checkout, environment, and model cache are disposable and do not enter
the repository.

## 20. Skeleton-skinned semantic arm — isolated pass, composite fail

`semantic_layer_import` restores a See-through crop to its declared 1280px
canvas, downsamples by an exact integer factor, and copies authoritative source
pixels over generated visible pixels. `mouse_semantic_arm_v1.json` maps the
accepted `handwear-l` candidate to `upper_arm_b` and `forearm_b`. A 4px regular
grid produces 286 vertices and 504 triangles; joint-local linear weights blend
across six pixels around the elbow.

The isolated part passed: neutral changed zero pixels and neutral, contact,
passing, and airborne each remained one connected component while retaining
100.0%, 87.1%, 98.7%, and 86.9% of neutral opaque area. Tightening the visible
mask also removed belt pixels that initially moved with the arm.

The full composite nevertheless failed human review. The static torso artwork
still contained the original arm, so moving the semantic arm revealed a ghost
copy. The automated gate had measured the isolated arm but not exclusive
ownership across the complete character.

## 21. Exclusive ownership and torso underpaint — pass

The corrected spec uses three deliberate layers:

1. generated `topwear` underpaint behind the original arm location;
2. the complete skinned semantic arm; and
3. authoritative body pixels with the arm ownership region removed.

Pose-specific order keeps hidden sleeve pixels behind the body until the arm
crosses the foreground. C++ now clips generated completion to the approved
source alpha, subtracts explicit ownership regions, measures the full visible
stack, and optionally requires exact neutral composition.

The final gate reports all 18,974 source pixels singly owned, with zero unowned,
multiply-owned, or outside-source pixels. The complete neutral composite changes
zero RGBA pixels. Moving poses expose coat underpaint rather than the source arm;
the intended other anatomical arm remains, but the duplicated moving arm does
not. The isolated mesh retains its prior component and area results. Native
48px evidence passes, and focused Catacombs review at 0.5x, 1x, and 2x reports
no objective findings.

Verdict: the ghost was an ownership/compositing defect, and that boundary
passes. It does not accept the deformation solver; exclusive ownership and
underpaint must remain fixed inputs to the next A/B experiment.

## 22. Human mesh-deformation review — linear blend rejected

Human review found that the mesh does not visibly morph the painted sleeve
around the skeleton. This matches the implementation: vertices outside a
six-pixel elbow band follow one rigid bone, and only the narrow transition
interpolates. Triangle rasterization moves the texture correctly, but no solver
propagates the joint constraints through neighboring mesh geometry.

The connectivity and exact-neutral gates therefore proved only structural
correctness. They did not prove useful deformation. Contact and airborne each
lose roughly 13% of neutral arm area, consistent with a compressed linear-blend
joint. The visual verdict supersedes the earlier conclusion that ARAP was not
needed.

Verdict: freeze the accepted semantic arm, ownership masks, underpaint, skeleton,
poses, and renderer, then measure before changing the solver. Section 23
supersedes the ARAP recommendation.

## 23. Measured diagnosis — three problems, not one

Section 22's review named the smallest of three. `layered_puppet_diagnostics`
measured all three on the frozen `semantic-arm-v5` inputs; frame digests were
byte-identical, so nothing about the render moved.

| problem | measure | value |
|---|---|---|
| old arm pixels stuck to the body | orphan islands inside the arm bounds | 105 px, 3 clumps |
| the tail and a belt sliver ride along with the arm | visual | see `parts/front_arm.png` |
| nothing painted behind the arm | backfill uncovered | 876 of 1,975 px |
| elbow folds when it bends | folded triangles on artwork | 14 of 504 |

The tail has the same cause as the stuck pixels, in the other direction. The
hand-drawn polygon reaches x198 while `handwear-l` ends at x186. One polygon
leaves arm pixels behind, the other takes body pixels along. It is why the
airborne arm reads as a stick.

The first two are what human review actually saw: a floating sliver, and a white
gash down the coat. Neither is a deformation problem.

Both existing gates were blind to them. Ownership only asks whether each pixel
has one owner, and a leftover arm pixel owned by the body has one owner. Neutral
is exact only while the arm still covers its own hole.

Bend angle predicts the area loss. Passing bends 6 degrees, folds nothing, keeps
98.7%. Contact and airborne bend 59 and 62, fold 43 and 44, keep 87.1% and 86.9%.

Two fixes removed both causes of the elbow damage.

Skinning now averages the two bone angles instead of two rotated positions, which
is what a rotation blend should be. The mesh is trimmed to the arm plus one cell
of collar, and the blend band widens with distance from the bone
(`joint_blend_lateral_scale`, 1.0). Folding is driven by lateral offset:
rotating about the joint by an angle changing at rate `g` gives a Jacobian
determinant of `1 - g*d` at lateral distance `d`, so the sleeve's far side
crosses over while the axis is safe. Widening only out there lowers `g` where `d`
is large. Threshold is `scale > bend/2`; the 62-degree pose predicts 0.54 and
folds measured zero at 0.5.

Every pose now has zero folds, stays one connected component, and neutral is
still pixel-exact.

**Retained area went down, and that is correct.** Contact 87.1% to 81.9%,
airborne 86.9% to 81.0%. The picture improved: before, the elbow had a notch
bitten out of it and the upper sleeve read as a detached blob; after, it is a
smooth continuous bend. A bent tube covers less area than a straight one, so the
95% area target was rewarding rigidity. Area is retired as a gate. What remains:
zero folds, one component, exact neutral, and review at 48px.

The two versions are near-indistinguishable in the 48px composite, which is the
expected size of the effect on a ~70-pixel arm.

## 24. Ownership from the layer alpha plus bone reach — pass

A skinned part now derives what it owns instead of being given an outline. A
pixel is owned when the grown candidate alpha paints it, the source paints it,
and it is within `reach` of the bone chain, where reach runs from a wide value at
the shoulder to a tight one at the hand. Wide at the shoulder because a sleeve
really is part of the coat; tight at the hand because the limb is free. The body
then subtracts exactly that region by name rather than repeating an outline.

Orphans fell 105 to 0, backfill 876 to 598, and the tail stopped swinging with
the arm. Neutral is still pixel-exact and all 18,974 pixels stay singly owned.

The tail is what justified the reach term. Alpha alone already drops it, since
`handwear-l` ends at x186 and the tail sits at x186-200. But the first reach
tried, 12 at the hand, cut through where the hand and tail root touch and severed
the tail into an 83 px floating island; the orphan gate caught it. 10 is the
largest value that passes, and it matches the hand's radius from the wrist.

One bug worth remembering: subtracting the mask from the ownership record but not
from the rendered artwork made every gate report a clean decomposition over a
composite that still drew the ghost arm. Ownership records and rendered pixels
must lose the same region.

## 25. Audit of earlier assumptions — four things we had wrong

**The legs never move.** Zero pixels differ in the leg, boot, head and ear
regions between neutral, contact and passing. Only 3 of the 10 bones are bound to
a part: `torso` carries the whole body rigidly, and the two arm-B bones carry the
arm. The poses declare `knee_a`, `foot_a`, `knee_b`, `foot_b`, `elbow_a` and
`hand_a` positions that nothing consumes. The earlier 10-part `mouse_profile_v1`
run did move legs, ~2,500 px per pose. Every "four-pose" review of
`semantic-arm-v5` has therefore been reviewing a standing mouse with one arm
moving. Airborne differs only because the body translates up 12 px.

**Interior holes were dismissed on a stale reading.** This document claimed all
four poses report 174-175 holes, so the count was the source art's own gaps. That
was read once on the first diagnostic run and never re-read. Neutral, passing and
airborne sit at 174; contact is 355. Those 181 extra are enclosed tears — a slash
at the shoulder and a hole between the hand and the tail. The metric works;
compare each pose against the neutral count.

**Ledger row 22 overclaimed.** "18,974 singly owned pixels; exact neutral; no
ghost" passed on a check that only asks whether each pixel has exactly one owner.
105 arm pixels welded to the body satisfy that. A decomposition gate that never
looks at the composite will keep passing over visible damage — the same shape of
error recurred later when an ownership mask was subtracted from the record but
not from the rendered artwork.

**The inputs were one `git clean` from gone.** `experiments/character_binding/out/`
was fully git-ignored, including `source-color.png` and the See-through layers.
Neither is reproducible from anything in the repository: the reference is the one
accepted identity out of a rejected fuzz set, and See-through ran for 520 seconds
in a disposable environment on another machine. `.gitignore` now excepts those
20 files, 880 KB.

**See-through gives less than section 19 implies.** Compositing all nine kept
layers leaves 21% of the source uncovered, 100,994 of 480,187 px. The two largest
gaps are the mouse's own ears, 43,568 px and 39,432 px, which appear in no layer
raw or optimized — `raw/ears.png` is the hallucinated human ears at the face
sides. A 2,748 px brown appendage beside the right paw, most likely the tail, is
also in no layer and extends past `handwear-l`'s crop box.

`raw/head.png` holds the genuine pixel-art mouse face, 71,416 px with the eye,
muzzle and pink nose in the correct palette, and it was dropped when `optimized/`
was cut — `info.json` part ids skip 2 and 5. It is recoverable and should be if a
head part is ever wanted. The ears are not recoverable from any layer.

Also: `back hair`, `headwear`, `ears-l` and `ears-r` are smooth antialiased
renders rather than pixel art, so anything assuming a uniform pixel grid across
`optimized/` is wrong for those four. `src_img.png` carries a white gap and a
ground shadow, so its alpha is not a character mask. The red scarf is baked into
`topwear` and cannot move separately. The raw-to-optimized crop is otherwise
lossless; opaque counts match exactly for every retained layer.

Two smaller items: `frame_size` is read as `[height, width]` and See-through's
canvas is square, so a non-square canvas would silently transpose every layer;
and the approved source reaches 256 px by an exact ÷4 while the See-through
layers go 1024 → 1280 → 256, so completed pixels are interpolated relative to the
source. `PreserveSemanticVisiblePixels` hides that at neutral, and it shows only
in the sleeve exposed when the arm moves.

Verdict: pass, gate on.
Stop clipping the underpaint, then stretch to close what is left. Plan and
fallbacks in
[`docs/character-layer-deformation-experiment.md`](../../docs/character-layer-deformation-experiment.md).

## 26. How to review a pose without getting it wrong

Do not identify parts by eye. At 48 px, upscaled, the moved arm and the static
one are not reliably distinguishable, and several wrong calls recorded here came
from stating a visual conclusion before measuring it.

Tint instead: composite the pose, then paint every pixel that
`part-poses/<part>/<pose>.png` marks opaque in a flat colour. That answers "which
of these moved" with no judgement. Bounding boxes from the part poses give the
same answer in numbers.

The wrong calls, kept for the pattern: retained area was predicted to rise and
fell six points; interior holes were dismissed on a count read once and never
re-read, while contact had gone to 355; "stop clipping the underpaint" was
planned and measured to do nothing; and the arm draped across the chest was
called the static one when it is the moved one. Each was a conclusion formed
before the measurement existed.

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

The current shipped Blender source remains reproducible, but it is a pipeline
proof rather than accepted art. The C++ layered-puppet and semantic-import
implementations, `mouse_semantic_arm_v1.json`, isolated part poses, native
frames, manifests, focused Catacombs review, and
[`character-layer-deformation-experiment.md`](../../docs/character-layer-deformation-experiment.md)
own the replacement direction evidence.

See-through and generated review output remains gitignored under
`out/see-through-v1` and `out/semantic-arm-v5`; revision, settings, input
digest, mesh dimensions, pose digests, component counts, pixel counts, and
rejection reasons are recorded above. External Python environments and model
caches remain outside the repository.

---

# What I would do next, in order

The ARAP A/B that used to head this list is withdrawn; see sections 23 and 24.

1. **Done.** Backfill closed by stretching, 745 px to 0. Removing the clip did
   nothing; `topwear` simply does not paint there.
2. **Clear the two gates the shoulder correction exposed.** 149 orphan pixels and
   4 folded triangles, neither tunable with the current knobs. The tail needs to
   become its own part.
3. **Bind more than one arm before believing a pose review.** Only 3 of the 10
   bones drive a part today, so the legs and head are frozen; the four-pose
   evidence is a standing mouse with one arm moving.
4. **Reach for MLS driving the existing mesh** if the weight and reach heuristics
   keep failing. Do not delete the mesh; it is the rasterizer and it holds the
   exact-neutral proof.
5. **Then apply the method to `handwear-r`,** split footwear, obtain complete
   legs/tail, and bind legs through hip/knee/foot.
6. **Defer skeleton-conditioned ML.** The blocker is the layer cut, not semantic
   parsing.
7. **Finish production after the complete-character gate.** Render/import the
   six replacement clips, expose editor import controls, record live
   transitions, then unblock runtime M4.
