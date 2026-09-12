# Boot surface control: who owns the view?

**Closed: all three candidates rejected; work paused at the user's request.**
The initial favorable judgment of depth 0.45 and its proposed repeat are
withdrawn. See the [session closeout](../../../../docs/history/sprite-boot-control-2026-09-10.md)
for measured results, the user's side-profile heel-up correction, and the
distinction between observed input assumptions and unproven causes.

Prepared 2026-09-10. The exact inputs are in `input-review.png`. At preparation
time, no files had been uploaded and no provider requests had run.

## Question

The isolated-leg generators can closely preserve an outer contour while still
painting the boot as a familiar front/three-quarter view. This experiment asks
whether real depth from the reviewed fixed-camera proxy prevents that semantic
reinterpretation.

This is a reusable-pipeline decision, not another general prompt sweep. The
authored proxy owns silhouette, scale, placement, heel-to-toe direction, camera
view and visible surfaces. Diffusion is allowed to own leather and pixel-art
appearance only.

## Inputs and comparison

- `edit-input.png`: the brown boot proxy and img2img prefill.
- `canny.png`: edges of that unannotated image.
- `depth.png`: the proxy renderer's z-buffer. Black is background and brighter
  boot pixels are nearer the fixed camera.
- `surfaces-review.png`: an inspection view only. It is never a model input.
- `identity.png`: the original mouse, used only for palette and finish.

All three jobs use SDXL base, pixel-art LoRA 0.8, IP-Adapter Plus 0.3, Canny
1.0, denoise 0.65, 30 DPM++ 2M/Karras steps, CFG 5.5, and seed 17290401.
`canny-only` is the baseline. `depth-0.45` and `depth-0.80` add the same actual
depth map through the installed xinsir SDXL depth ControlNet; only depth
strength and output name change between that pair. Depth ends at 85% of the
sampling trajectory so the final steps can resolve pixel-art materials.

The full square canvas maps back to the fixed 96px crop `[88,136,184,232]`.
No result may be fitted, rotated, warped, recentered or silhouette-clipped.

## Decision declared before generation

Judge surface interpretation first: the toe must travel down-left as a
side/upper view, without a round frontal toe cap or exposed sole. Then inspect
silhouette/registration and finish separately. Canny-only is expected to
preserve contour but may repeat the wrong view. Depth is useful only if it fixes
the view without making the output mannequin-like.

If neither depth result fixes the view, stop generative boot rendering. Keep
the deterministic proxy or an authored boot-view atlas as production geometry,
and restrict generation to texture/reference donation. If depth works, repeat
the chosen setting once, test the opposite view, then expand to the smallest
discrete boot-view atlas needed by the twelve-frame cycle.

`manifest.json` retains source hashes, z-buffer convention, graph hashes and
job state. Run reviewed jobs with `scripts/run_comfy_leg_pose_trial.py`; despite
its historical name, it accepts any ComfyUI job declared by a retained trial
manifest and refuses changed inputs or ambiguous resubmission.

## Results

Three local requests completed. All retained the fixed silhouette and
registration: IoU is 0.985–0.988, area is within 1.2% of the target, and
centroid drift is below 0.03 working pixels. Those scores are deliberately not
used as a view metric.

- Canny-only repeats the incorrect lace-up front/three-quarter interpretation.
- Depth 0.45 was initially misclassified as the intended view. User review
  correctly rejected it: the result is not a side-profile recovery foot with
  its heel lifted because the supplied proxy itself encodes an oblique boot.
- Depth 0.80 follows the boxy proxy too literally and loses useful material
  definition.

Do not repeat any of these graphs. Frame 10 is late support on the far leg; the
near leg is folded in recovery, with its calf traveling back/up, heel raised
and toe hanging down in side profile. The trace's knee→ankle→toe chain encodes
that phase, but the boot proxy reduces it to a heel-to-toe axis, uses a 35°
oblique camera and defaults the shaft to the boot's local orientation. Audit
the twelve traced phases and author explicit heel/sole-contact information
before preparing another generator input. The retained
[phase audit](../run-phase-audit-v1/README.md) shows the unchanged drawings and
complete traced leg chains side by side. Exact visual judgments are in
`assessment.json`; measured results are in `summary.json` and `review.png`.
