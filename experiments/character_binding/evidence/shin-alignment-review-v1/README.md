# Straight lower-leg guide correction

2026-09-10. The user selected independent rendering and rejected the isolated
folded leg's shin angle, while liking the trouser folds and boot finish.
No new image-generation request has been made for this correction.

Subsequent user clarification: the original guide already reads as the intended
nearly horizontal calf with its toe pointing down. The generated calf curves
down and turns the toe right. The small cuff-axis adjustment below does not
fix that larger output distortion. Its cause has not been established; these
guide measurements should not be presented as an explanation of the model's
error. The next test holds the original v2 pose fixed; see
`../leg-pose-preservation-v1/README.md`.

There is a separate mismatch in our input geometry. In v2 the whole boot, including
its shaft, rotates from its sole direction; a separate 2D calf then connects
the knee to the resulting cuff. Those directions need not agree. The pose-10
near leg changes direction by **29.5° at the cuff**. Across the twelve frames,
the largest such guide mismatch is about 60.2°. These measure our guide axes,
not the anatomy of the generated image or a medically defined joint angle.

V3 gives the shaft its own orientation along the knee-to-proxy-ankle line.
The cuff lies between that knee and ankle, so the calf and shaft remain
collinear. The foot and sole retain their previous placement and pitch and
articulate relative to the shaft at the ankle. The intended knee bend remains.
The maximum residual mismatch across all 24 leg positions is below 0.0001°.

This is a guide correction, not a newly finished leg. The generator still has
to follow it. The proxy ankle is explicitly defined inside the boot model;
these are candidate binding landmarks, not a silent change to the original
puppet document's ankle joints. The source puppet is unchanged.

`comparison.png` shows the old guide, corrected guide and rejected generated
drawing on the same crop. Markers appear only on the guides: knee, cuff and
proxy ankle. `old-part.png` and `corrected-part.png` retain full 256px geometry;
`corrected-input-1024.png` enlarges the same 96px crop used by the isolated-leg
experiment. `measurements.json` retains all before/after axes and anchors.

Source configuration: `../../inputs/boot-view-guide-v3.json`. Full twelve-pose
guides are in `../boot-view-guides-v3/`. Reproduce this comparison with
`scripts/review_shin_alignment.py --help`. Six focused geometry tests pass,
including all-pose shaft alignment and unchanged sole pins. No new art or
production asset has been accepted.
