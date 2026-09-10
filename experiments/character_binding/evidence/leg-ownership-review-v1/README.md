# Leg ownership at the crossing

Diagnostic only; no generation requests. Prepared after the user identified
the knee-to-boot assignment error in stout redraw pose 10.

`reference_10/ownership-review.png` shows the intended complete near leg A
(orange), complete far leg B (blue), their actual order A-over-B, and the
flattened material guide. In this pose A has the forward knee and folds back
to the high boot. B continues to the low supporting boot. The coat normally
hides parts of both chains. Pose 4 provides the opposite case: A stands in
front while B folds behind.

The complete leg geometry and hip/knee/cuff/sole paths come from the existing
v2 guide config, retained boot anchors and unchanged puppet poses. They are
not inferred from the incorrect generated picture. `near-complete.png` and
`far-complete.png` retain those full geometry layers on their original 256px
canvases. Cropping/zoom affects only the labeled diagnostic board.

The draw order is already far leg/boot, then near leg/boot. The ambiguity is in
the flattened signal: both trouser legs use identical material colors; the
surface-ID view labels both as yellow. Neither communicates persistent leg
identity to the model. The earlier broad-pose check (one high boot and one low
boot) missed the incorrect connection. A wider composite cannot fix an error
already present in the raw model drawing.

Next proposed test: one pose-10 redraw with an explicit A/B identity/occlusion
guide, compared with two independently completed leg layers composited in the
known order. The labeled guide is an ordinary image reference, not a trained
ControlNet condition or a guarantee. Separate layers give the compositor
ownership of the crossing; each leg's shape, registration and boot connection
still need checking. Do not recolor these IDs into final trousers or treat
this diagnostic as finished art. New actual generator inputs should be shown
before submission.

Reproduce with `scripts/review_leg_ownership.py --document ... --guides ...
--output ...`. It uses the same boot projection and leg widths as the source
guide and refuses mismatched document hashes. It does not change the puppet
or its accepted motion. The diagram font uses Arial when available, with a
Pillow default-font fallback; font choice does not affect the geometry layers.
