# User review: full-resolution painted animation

This review supersedes the open-ended request to approve atlas v2. Original
raw generation outputs and atlas renders are preserved unchanged.

## User's judgment

- The generated isolated legs have very good appearance. Preserve their
  natural boot shapes, volume, leather finish and trouser folds.
- The atlas animation is relatively smooth at 96px, but its legs are inferior.
- `reference_07/far` has too much heel behind the ankle and looks animatronic:
  an inverted T instead of an L.
- Work toward a reasonable full-resolution animation. The user asks what this
  experiment establishes and whether to continue in a fresh conversation.

The supplied images are retained as
[generated appearance target](generated-appearance-target.png) and
[atlas geometry feedback](atlas-geometry-feedback.png). Approval of appearance
does not erase the measured registration drift in the raw generated sheets or
approve every generated pose.

## Verified explanation

Atlas v2 `reference_07/far` has ankle `(169.999, 199.110)`, heel
`(155.253, 206.974)` and toe `(186.711, 202.059)`. Projecting the ankle onto
the 31.840px heel-to-toe axis places it 49.57% along the sole. That explains
the large backward heel: the shaft meets the foot near its middle. The heel
was transferred from an authored source estimate. It was not validated by
the user accepting the different frame-10 recovery profile.

The atlas bakes cloth from a 20×25px donor crop and leather from a 24×22px
crop into four 128px material tiles. It does not preserve the complete generated
leg drawings. Procedural outlines, gradients and folds replace their design.
The resulting parts are rasterized to a 256px full-character canvas, then
downsampled to 96px for the enlarged preview. The original character source
is also 256×256. The raw sheet is 1536×1024 across six cells; this is not the
resolution of an individual full-character frame.

## Conclusion

The experiment demonstrates deterministic attachment, layer ownership, contact
placement, cyclic coat response and reusable mapping. It does not demonstrate
correct anatomy for every authored heel, preserved painted quality or a polished
full-resolution animation. Exact pin and alpha tests are necessary structural
checks; they are not aesthetic or anatomical validation.

## Brief for the next conversation

Continue the green-coated mouse run using this review and the active handoff.
Prioritize a full-resolution painted animation that retains the generated legs'
quality. Keep C++ authoritative for reviewed geometry, pose, semantic anchors,
view selection, contact and draw order. Preserve all raw experiment evidence.

1. Correct the contact-boot construction, starting with frame 7 far. Define
   anatomical ankle, heel, toe and sole contact separately; compare the entire
   boot shape against the preferred generated artwork. Audit the opposite
   contact and recovery boot so a local fix does not conceal another bad view.
2. Preserve complete generated parts as artwork. Build an explicit high-detail
   part/anchor/view manifest and test limited semantic registration or mesh
   corrections. Show original and corrected artwork separately. Avoid arbitrary
   per-cell bounds fitting and the old tiny-sample shading substitute.
3. Use a declared full-character master canvas. 512×512 is the assistant's
   proposed initial experiment size, not a user-approved shipping resolution.
   Keep raw part detail until composition, and address the 256px upper-body
   source consistently; interpolation of that bitmap alone adds no detail.
4. Render the same twelve accepted source phases at master size, with 96px
   playback as a secondary view. Judge natural boot anatomy, attachments,
   consistent volume, folds and texture stability through the closed loop.
5. If the full-detail twelve-frame result is stable but the cadence still feels
   stepped, evaluate a separate 24-frame/24fps cycle with intermediate C++ poses
   and artwork corrections. Resolution and temporal sampling are separate axes;
   more frames cannot repair the current heel geometry.

Show exact input artwork and geometry before any further generation, per the
standing user requirement. No new generation, resolution conversion, runtime
change or production import was performed while recording this review.
