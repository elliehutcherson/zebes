# Isolated folded leg: preserve the approved pose

Prepared 2026-09-10. **Zero generation requests; nothing uploaded.**
The user asked to start this experiment and asked whether to move to a fresh
conversation. Actual new reference/control images are shown in
`input-review.png` before generation, as they requested.

## Question and control

Can a renderer add the successful fabric/leather finish while preserving the
approved folded calf and downward-pointing boot? The rejected isolated output
curved the calf downward and turned the toe right. The recent v3 cuff-axis
adjustment did not address that output distortion; the measured 29.5 degrees
was a guide-axis difference, not evidence of its cause.

Use the **original v2 pose-10 near-leg edit target**, byte-for-byte unchanged
from `../leg-ownership-trial-v1/near/edit-input.png`. Keep the same original
mouse appearance reference and fixed crop `[88,136,184,232]`. Neither the v3
guide nor the malformed generated drawing is an input. Stoutness, source pose,
scale, placement and boot direction stay fixed.

## Three initial trials

1. **Built-in image generation:** `builtin-prompt.txt`, with ordered references
   `edit-input.png`, `identity.png`, `landmarks.png`. Ask for surface repainting
   while preserving the silhouette. The contour/landmark image is an ordinary
   reference, not a hard constraint or a trained skeleton control.
2. **ComfyUI Canny, denoise 0.65:** `canny-0.65.json`.
3. **ComfyUI Canny, denoise 0.85:** `canny-0.85.json`.

The local pair changes only denoise and the output filename. Both use SDXL
base, pixel-art LoRA 0.8, IP-Adapter Plus 0.3, Canny strength 1.0 throughout,
30 DPM++ 2M/Karras steps, CFG 5.5 and seed 17290401. These are starting settings,
not an established optimum. The built-in versus local comparison changes the
model and conditioning route, so it is exploratory rather than a causal A/B.

`canny.png` contains OpenCV Canny edges of the **actual unannotated RGB target**,
thresholds 100/200 at 1024px. No skeleton dots, labels, ordinal depth or surface
IDs reach ControlNet. The separate `landmarks.png` includes the outer contour,
knee-to-cuff line and heel-to-toe arrow. The proxy ankle is explicitly a boot
model landmark; the original puppet's ankle joints have not been rebound.

The whole isolated canvas is redrawn without a sampler mask. Keep the raw
white-matte result. No silhouette clipping, warp, boot rotation or per-result
recentring may hide a miss. No depth map is used in this test. The current
1024px request size is for completion; it does not settle final sprite size.

## Evaluation, declared before generation

Keep raw dimensions and provider provenance. Map the full output canvas back
to the same 96px crop, then place it at `(88,136)` on the 256px working canvas.
If the output is not square, stop to review the mapping. Do not fit its bounds.

Compare each candidate against the guide at identical coordinates:

- Inspect the complete calf silhouette. Does it stay nearly horizontal, or
  acquire the rejected downward curve? Good endpoints alone cannot prove this.
- Annotate the actual output knee, cuff, ankle, heel and toe in working pixels.
  Compare displacement and heel-to-toe direction against `manifest.json`.
  Mark a landmark ambiguous rather than inventing a precise measurement.
- Overlay the guide outline and report silhouette overlap and added/missing
  foreground pixels as supporting diagnostics. Those numbers do not establish
  anatomy, leg ownership or art quality.
- Review folds, leather finish, outlines and stoutness separately. Preserve
  native/raw views as well as a 48px whole-character composite when appropriate.

The initial decision is visual and limited: choose a candidate worth repeating,
or identify that none follows the pose. If one is promising, repeat it once,
then test the opposite crossing (pose 4) before expanding to twelve frames.
If style conditioning keeps changing anatomy, an isolated boot and trouser
completion comparison is the next bounded fallback. Post-training remains
later work requiring accepted targets and held-out poses.

## Execution handoff

`manifest.json` stores input/prompt/graph hashes, exact landmarks, requested
canvas, registration, and the three prepared jobs. Live read-only inspection
confirmed the installed xinsir Canny model and `ControlNetApplyAdvanced` schema
on derry. No sampler or upload ran. `scripts/run_comfy_leg_pose_trial.py` is the
dedicated local runner; it reuses the gap runner's byte-verified upload helper.
It validates retained hashes, refuses to retry an ambiguous submission, resumes
known prompt IDs, and saves raw node `16` before fixed-crop mapping. Run the
two jobs sequentially after input review and refresh any remaining node/model
checks first. The built-in request uses the listed prompt and reference paths.

```bash
build/tileset-venv/bin/python scripts/run_comfy_leg_pose_trial.py \
  --trial experiments/character_binding/evidence/leg-pose-preservation-v1 \
  --name canny-0.65
```

Repeat with `--name canny-0.85`. Network access uses the existing SSH loopback
tunnel. Output receipts live under `results/<name>/`; check them when resuming,
especially if submission was interrupted. `provider_calls` counts confirmed
requests, not an unacknowledged request that may still need reconciliation.

Reproduce the inputs using:

```bash
build/tileset-venv/bin/python scripts/prepare_leg_pose_trial.py \
  --source-trial experiments/character_binding/evidence/leg-ownership-trial-v1 \
  --output experiments/character_binding/out/new-leg-pose-input-review
```

The preparation script makes no provider calls. Three focused tests cover the
approved target/crop, the local pair's single changed parameter, and both
sampler conditionings with the encoded prefill. Three runner tests cover changed
input rejection, ambiguous submission refusal and resumption without fitting.
