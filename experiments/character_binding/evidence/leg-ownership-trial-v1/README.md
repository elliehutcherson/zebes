# Pose-10 leg ownership comparison

Three built-in `image_gen` requests completed on 2026-09-09: one combined
redraw with explicit A/B leg identities, and two independently completed legs.
The user approved this comparison. The planned opposite-crossing follow-up is
retained separately in `../leg-ownership-pose04-v1/` (one additional request).
No ComfyUI/model-training requests were made in this round.

Open `review.html` for the previous result, labeled-guide redraw, independent
leg composite, isolated assets and pose-4 follow-up. This is a focused crossing
test, not a complete twelve-frame animation or an accepted production asset.

## Findings

The pose-10 labeled-guide result gives the folded calf a more readable
horizontal connection to the high boot, crossing in front of the nearly
vertical standing leg. This appears to improve the knee-to-boot assignment
identified by the user. The tested package includes the A/B diagram, more
explicit connection wording and a broader mask declared before generation;
the trial does not isolate the contribution of color labels alone.

Independent generation produces one connected folded leg A and one standing
leg B. The compositor deterministically puts B behind A and the original coat
above them. It cannot exchange which output owns the crossing. However, each
model output can still change its individual geometry and boot view.

With the predeclared crop inverted without fitting, A's opaque bounds change
from `(113,141,163,181)` in the guide to `(113,143,162,192)` in the output:
the folded boot reaches about eleven working pixels lower. B changes from
`(133,141,169,215)` to `(132,141,168,214)`, much closer to the guide. Bounds
describe registration, not anatomical landmark accuracy. The isolated route
therefore needs explicit part-to-rig calibration and boot-view checks before
expanding to other poses. No corrective fitting conceals this difference here.

The labeled whole-character route is the more promising immediate visual
candidate in this small comparison. Independent layers remain useful for
structural ownership, reusable artwork and later garment changes. Neither
result establishes consistency over a full cycle. The pose-4 follow-up checks
the reversed visible overlap but has only one sample and partially hidden
connections; treat its readability as a visual judgment, not a guarantee.

## Inputs, registration and compositing

`manifest.json` and the three directories retain exact prompts, input files,
reference roles and raw-output hashes. The original mouse is the same fixed
appearance reference throughout. Colored A/B labels are ordinary image
references, not a trained semantic ControlNet channel or final costume colors.
The combined redraw also receives a separate region guide. It preserves
original RGB and alpha outside that mask in `registered-rgba.png`.

For the isolated requests, both source assets use the identical **96×96 crop
at `(88,136)–(184,232)`** from the original 256px coordinate system. That crop
was chosen before generation and enlarged to the requested 1024px canvas.
The received whole canvas is resized to 96px and placed back at `(88,136)`.
There is no per-part bounding-box fitting, centering or later rescaling.
White matte extraction supplies alpha; silhouettes are not clipped to the
guide. Complete hidden portions remain in each leg asset.

All three raw outputs arrived as **1254×1254**, opaque, despite the 1024px
request. `raw-output.png` is untouched. Mapped crops, registered parts and
combined/separate composites remain distinct. The model identifier and seed
are not exposed by the built-in tool. Actual provider source paths and call
counts are in the manifest.

The declared compositing order is `far_arm, B, A, tail, body, near_arm, head`.
Original per-part alpha is retained, so antialiased overlaps can blend against
the new legs; this is actual layer composition rather than an opaque mask
copy of the old whole sprite. `separate-legs.png` exposes the two generated
parts without the coat so the crossing can be inspected directly.

## Reproduction and verification

`scripts/leg_ownership_trial.py` prepares inputs, retains received output and
composites without provider calls. `scripts/render_leg_ownership_trial.py`
rebuilds the review. Use `--help` for explicit paths. Preparation refuses
existing directories, and receiving a completed result cannot silently replace
it. Three focused checks cover fixed crop inversion, original RGBA protection
and near/far/coat compositing order. Browser review checks the comparison views
and native-size presentation. No production import was performed.
