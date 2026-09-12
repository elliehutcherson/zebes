# Digitigrade run references — 2026-09-12

Research evidence, not a replacement for the mouse plan or an accepted gait.
The user described run 02 as "this isn't great" and requested internet examples
of digitigrade characters running, then asked about downloadable assets to inspect.
No new mouse animation or rig change was authored during this reference study.
After seeing the diagnostic animation, the user said, "Okay, that run animation
looks good," and suggested a slight mouse foot-shape adjustment. The
[plan of record](../../docs/mouse-3d-plan.md#immediate-next-step-foot-shape-and-articulation-study)
now records the bounded study that follows from this feedback.

## Downloaded and inspected

**Low Spec Velociraptor**, by **pistachio**:
[original asset page](https://opengameart.org/content/low-spec-velociraptor).
License: [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The unmodified original archive is `low_poly_raptor_v2_oga.zip`; the extracted
Blender file and textures are in `low_poly_raptor_oga/`. The direct download is
<https://opengameart.org/sites/default/files/low_poly_raptor_v2_oga_0.zip>.

- [Original Blender file](low_poly_raptor_oga/low_poly_raptor.blend)
- [Rig/action inventory](inspection.json)
- [Animated diagnostic review](run-inspection-wide/run-rig-review.gif)
- [Four sampled poses](run-inspection-wide/pose-comparison.png)
- [Sampled action](run-inspection-wide/sampled-run.json)
- [Joint motion measurements](run-inspection-wide/joint-motion-summary.json)
- [Original file hashes](provenance.json)

Blender 4.0.2 loaded the source with embedded script execution disabled. There
are no embedded text blocks. The file contains `run`, `walk`, two attacks, a
preview pose and a pose library. The raptor's source rig/action/weights were
not changed or resaved. The diagnostic changes are a fixed side camera,
Workbench flat coloring, hiding the alternate feathered mesh, and a separate
color-coded diagram from the measured joint positions. Attribution accompanies
the derived renders. The diagram uses equal horizontal and vertical scale.

The deforming chain includes `thigh.l -> leg1.l -> leg2.l -> foot.l`: thigh,
shin, raised foot/metatarsal segment and toes, plus the mirrored right side.
Its source run articulates the raised foot segment separately from the toes.
Over integer samples 0–15, the angle between those last two segments varies
from approximately **19.9 to 57.5 degrees**. This measures animation, not an
anatomical limit or a prescribed mouse joint angle.

The author advertises a 16-frame run. The file is set to 30fps; its legs close
at frame 16, but some arm channels have closing keys at 18. All sampled joints
close at 18, with the legs holding their end pose after 16. The diagnostic GIF
uses the advertised 16-frame interval (0–15); this is useful reference evidence,
not an assertion that the asset has production-ready loop/contact behavior.
The actual ground contacts were not quantitatively validated for this source.
Its horizontal torso is unsuitable as a posture target for the upright mouse.

## Video references examined

1. **Lars's Archive — IK adapter for digitigrade legs**, starting at
   [2:30](https://www.youtube.com/watch?v=OlvmiBkoLQM&t=150s).
   This is Unity/VRChat, not a Blender tutorial. The upright character's run is
   displayed at 20% speed with its skeleton visible. Paused and stepped frames
   around 2:34–2:40 show the trailing leg extending, then folding, and the
   opposite foot arriving under a flexing leg. The raised hock and toe section
   retain separate articulations. Useful upright movement reference; the creator
   describes it as procedural remapping and acknowledges overshoot edge cases.

2. **Pierrick Picaut — Advanced Dog leg rig in Blender**:
   [anatomy/motion analysis at 0:46](https://www.youtube.com/watch?v=2p_o6XmmTN0&t=46s),
   [IK mechanism at 6:45](https://www.youtube.com/watch?v=2p_o6XmmTN0&t=405s).
   Inspected the real-dog knee/hock/toe annotations around 0:53–1:03 and Blender
   bone arrangement around 2:22. The hock is the ankle; the segment from it to
   the toes is part of the foot. The creator describes a three-joint IK setup
   with an ankle rotation adjustment. This is a mechanics/rig reference, not an
   upright run to copy. The later IK chapter was located but not fully watched.

Additional candidates located, not downloaded or inspected frame by frame:

- [Royal Skies — Advanced Digitigrade Leg Rig](https://www.youtube.com/watch?v=NmQkv58wry4),
  a short Blender rig tutorial.
- [CG with Isuru — Raptor free advanced rig](https://cgwithisuru.gumroad.com/l/raptors),
  two fully rigged Blender raptors, described as free for noncommercial use.
  Animation demonstrations are linked; inclusion of keyed actions is unverified.
- [Layla Rig by Not a Fox](https://ko-fi.com/s/8d5870fb98), an upright anthro
  Blender rig with digitigrade IK. It is a paid candidate; no purchase was made,
  no downloadable run action was confirmed, and the live availability/price
  was not established by the partially rendered listing.

## Implication for the mouse

Run 02 passed its authored contact contract, but that only established a
planted rigid paw. It rotated the whole broad paw down around one mesh vertex.
It did not add a toe joint or independently deform the long foot/heel segment
relative to the toe pad. The references make that distinction worth testing.

The next study should block a small number of side-view poses with the pelvis,
knee, hock and toe pad visibly marked. Test compression and extension across
the whole leg while keeping the torso upright. Compare a targeted toe/foot
articulation with the retained rigid-paw result before deciding what rig or
weight repair is justified. Neither these references nor contact metrics
establish artistic acceptance. Preserve the accepted master and both runs.

## Reproduce the inspection

`inspect_reference.py` and `sample_run.py` use Blender and accept source and
output paths after `--`; `sample_run.py` requires a new output directory.
`package_inspection.py` uses Pillow and the adjacent `run-inspection-wide/`
renders. Remote workspace: `/tmp/zebes-digitigrade-reference-20260912` on Derry.
These scripts only read local assets and produce diagnostics; there are no
provider calls, model replacements, engine dependencies or production imports.
