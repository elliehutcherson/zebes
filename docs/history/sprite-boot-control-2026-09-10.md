# Sprite boot controls: rejected results and stop point

Closed 2026-09-10 at the user's request: document the results and stop here.
Six image requests completed (one built-in, five ComfyUI). No result was
accepted, no twelve-frame batch followed, and no runtime assets were replaced.
Further implementation and generation are paused until the user resumes work.

## Retained evidence

- [Isolated-leg inputs, outputs and method](../../experiments/character_binding/evidence/leg-pose-preservation-v1/README.md):
  unchanged v2 pose-10 near leg, original mouse appearance reference, and a
  contour/landmark reference for the built-in edit; actual Canny edges for
  the two SDXL redraws at denoise 0.65/0.85.
- [Isolated-boot inputs, outputs and method](../../experiments/character_binding/evidence/boot-surface-control-v1/README.md):
  Canny-only compared with Canny plus actual proxy z-buffer depth at strengths
  0.45/0.80. Surface-ID colors were for inspection only.
- [Twelve-frame phase audit](../../experiments/character_binding/evidence/run-phase-audit-v1/README.md):
  original pose drawings beside the retained leg traces; zero image requests.

Raw outputs, submitted local graphs, job receipts, input hashes, comparison
images, measurements and visual assessments remain in the evidence folders.
The built-in call's submitted wording is retained separately from its prepared
prompt in `leg-pose-preservation-v1/results/builtin/submitted-prompt.txt`.

## Results and corrections

| Trial | Silhouette IoU | Centroid drift in working pixels | Final disposition |
|---|---:|---:|---|
| Built-in isolated leg | 0.9301 | 0.3526 | Rejected boot interpretation |
| Isolated leg, Canny 0.65 denoise | 0.9865 | 0.0618 | Rejected boot interpretation |
| Isolated leg, Canny 0.85 denoise | 0.9856 | 0.0674 | Rejected boot interpretation and finish |
| Isolated boot, Canny only | 0.9854 | 0.0215 | Rejected boot interpretation |
| Isolated boot, depth 0.45 | 0.9871 | 0.0203 | User rejected; initial favorable assessment withdrawn |
| Isolated boot, depth 0.80 | 0.9877 | 0.0177 | Rejected boot interpretation and boxy finish |

Measurements use a uniform whole-canvas mapping back to the same 96px crop
on the 256px working canvas, without fitting or clipping the result to the
guide. The reviewer thresholds distance from the white matte and keeps the
largest connected foreground component. The scores measure agreement with
the supplied guide; they do not validate the guide's anatomy or camera view.

The initial claim that the built-in edit enlarged and recentered the part was
incorrect. Its raw canvas was 1254×1254 instead of the requested 1024×1024.
After the declared uniform mapping, area was 0.995 times the guide and centroid
drift was 0.353 working pixels. Local contour changes and the wrong boot read
remain visible, but canvas size alone did not establish a registration failure.

The more consequential error was calling depth 0.45 a promising side/upper
view and proposing another seed. The user rejected that interpretation:
the frame requires a **side-profile recovery leg with the heel lifted and
toe hanging down**. All six candidates are rejected. The proposed repeat is
withdrawn. Strong agreement with the proxy does not make its foot pose right.

## What is known and what remains unproven

The retained support annotations assign near-leg support to frames 1–5, flight
to frame 6, far-leg support to frames 7–11, and flight to frame 12. In frame 10,
the near leg is folded in recovery. Its source-cell trace is knee `(141,201)`
→ ankle `(99,184)` → toe `(86,199)`, so the ankle is above the knee and toe.
This is directionally consistent with the user's correction. It does not prove
that every joint, foot landmark or phase label is correct; the phase audit
visualizes existing annotations rather than independently validating them.

The trace contains no explicit heel landmark. The later boot guide does have
separate authored heel/toe anchors and sole directions; these did not come from
heel tracing on the supplied reference sheet. The proxy uses a 35° camera yaw,
8° elevation, a projected sole-axis calculation and a shaft attached in the
boot's local coordinates. Those are concrete input assumptions to re-examine.
The rejected views do not isolate which assumption causes the error, nor prove
that adding one heel point or changing camera yaw will solve it. Claims that
the source skeleton is definitely correct, or that one proxy setting explains
the entire failure, were too strong.

The useful finding is limited: the tested local graphs closely reproduce a
guide's contour while still failing the intended foot reading. A reusable
animation pipeline and correct twelve-frame run have not been demonstrated.

## State at stop

Retained tooling includes optional z-buffer export, preparation of the boot
comparison, manifest-selected output nodes in the ComfyUI runner, a fixed-canvas
reviewer and the source/trace phase audit. The previous implementation pass
reported 20 focused tests passing, Python compilation and `git diff --check`.
Those are software checks, not animation acceptance. Changes remain uncommitted.

The next proposed investigation, only if work is resumed, is to verify the
complete recovery-foot shape and its ankle/heel/toe relationships against the
source drawings, then show corrected geometry and conditioning maps before
any more generation. The current depth graphs should not be repeated.
