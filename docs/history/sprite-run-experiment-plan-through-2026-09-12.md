> Archived 2026-09-12 after the user accepted the storybook 3D mouse and
> selected its workflow as the plan of record. Statements below about current
> work, next steps or pending acceptance describe the earlier state.
> See the [current plan of record](../mouse-3d-plan.md).

# Twelve-frame mouse run: proposed experiment plan

**Resumed 2026-09-10.** The user requested a fresh audit, a new experiment and
autonomous execution. The [sprite sequence milestone](../sprite-sequence-milestone.md)
now owns that work. It identifies the guide/rig ankle disagreement, implements
an independent C++ profile control and records the shared-sheet experiment.
The user accepted its frame-10 profile as a starting point. The six earlier
outputs remain rejected; their [closeout](../history/sprite-boot-control-2026-09-10.md)
remains evidence, not the active stop instruction.

Prepared and **approved 2026-09-08**, with the existing green-coated mouse as
the target. Source calibration and deterministic rendering are in progress.
The first generation experiment has now run: four cleanup pilots and twelve
fresh white-matte frames. The [results](../history/posed-mouse-cleanup-2026-09-08.md)
include complete playback; final art review remains open.

The user's amendment takes precedence over the original sequence: fix the
source skeleton, limb assignments and twelve-frame puppet first; show the
actual artwork, skeleton overlays, poses and any conditioning maps for review
before every new generation setup. Old track/art/experiment gates are suspended
until an animation works. Numerical screens below are diagnostic suggestions,
not mandatory acceptance gates. Prior techniques may be retried with corrected
inputs if the current approach leaves a visible problem.

Later on 2026-09-08 the user supplied the actual twelve-pose drawing sheet and
rejected the inherited rig's motion. That sheet, retained as
`experiments/character_binding/inputs/run-pose-reference-12.png`, supersedes
Rig Bench as the pose authority. Each drawing is traced directly and compared
with the mouse; source binding and target gait are separate review views.

The best prospect is a complete layered mouse driven by authored motion, with
small corrective drawings and generation used to complete reusable parts. The
next-best generative experiment is constrained cleanup of an already posed
mouse. A temporal video model deserves a separate, bounded trial because the
recorded ComfyUI attempts did not test temporal conditioning.

These are engineering judgments, not measured success probabilities. All three
still depend on art review. Twelve frames alone do not guarantee smooth motion.

## Immediate priority and proposed revision

On 2026-09-10 the user selected **independent rendering** and rejected the
isolated folded leg's kinked lower leg, while approving its folds and boot
finish as useful visual reference. Keep the intended knee flexion and make
the shin/calf continue through the boot shaft; do not straighten the entire
folded leg or adopt the malformed result as ground truth.

The user's follow-up clarifies the actual defect: the guide's calf is already
nearly horizontal and its toe points down; the **generated** calf curves down
and the toe turns right. The later v3 cuff-axis adjustment is a separate input
experiment and did not fix that output. Its 29.5° guide-axis measurement is
not evidence of the cause. [Diagnostic comparison](../../experiments/character_binding/evidence/shin-alignment-review-v1/README.md).

The [pose-preservation comparison](../../experiments/character_binding/evidence/leg-pose-preservation-v1/README.md)
is complete: one built-in request and two local Canny redraws. Canny held the
outer pose and registration at about 0.986 IoU with roughly 0.1 working-pixel
centroid drift, while all three generators still painted a familiar
front/three-quarter boot. Contour plus verbal landmarks is not enough to own
the boot view.

The follow-up [boot surface-control experiment](../../experiments/character_binding/evidence/boot-surface-control-v1/README.md)
is also complete. It isolates one boot and supplies actual z-buffer depth from
the reviewed fixed-camera proxy. Canny-only repeats the wrong view, and stronger
depth follows the proxy more closely. User review then exposed that the proxy
itself is wrong: frame 10 needs a side-profile recovery foot with the heel
lifted, not an oblique boot whose overall axis merely points down-left. All
three results are rejected. Independent-layer completion and explicit binding
remain the selected route.

### Reusable pipeline milestone

Stop asking an image model to jointly invent pose, registration, occlusion,
camera view and appearance. The pipeline contract is now:

1. Zebes-authored rig/pose data and fixed-camera proxies own joints, silhouette,
   attachment anchors, view, surface visibility, draw order and loop timing.
2. Provider graphs may repaint reviewed isolated parts, but never become the
   authority for geometry or registration. Real Canny/depth inputs retain their
   actual semantics; diagnostic labels are never passed as fake controls.
3. A manifest-driven runner hashes inputs/graphs, resumes known jobs, refuses
   ambiguous resubmission and retains raw outputs. A fixed-registration reviewer
   measures silhouette separately from manual anatomy/view/art judgments.
4. Before another request, audit all twelve traced phases against the source
   sheet and show complete hip→knee→ankle→heel→toe/sole chains. The current
   trace has ankle and toe but no explicit heel/contact segment; add those boot
   semantics rather than inferring a 3D view from one vector. The
   [source/trace phase audit](../../experiments/character_binding/evidence/run-phase-audit-v1/README.md)
   displays the annotated support/flight sequence and exposes the missing heel
   trace data. It does not independently establish that the skeleton is correct.
5. Replace the 35° oblique recovery-foot proxy with a reviewed side-profile
   control whose raised heel and downward toe are unambiguous. Then test one
   repeat and its opposite-leg counterpart. If either fails, use authored boot
   views and generation only as a texture/reference donor.
6. Once the boot atlas is stable, complete the two underlying leg layers and
   render the full twelve-frame mouse deterministically at its defined logical
   pixel grid. Only then add coat/cape response and broaden the same part schema
   to equipment and new animations.

C++ remains the intended home for production pose sampling, atlas selection,
anchors and compositing. The experiment uses Python for retained PNG/ComfyUI
orchestration because `experiments/` is intentionally outside the engine build
and the existing provider tooling is already there. No runtime asset-format
change is justified until the twelve-frame result passes visual review.

Recent research does not justify returning to unconstrained frame generation.
[Sprite Sheet Diffusion](https://arxiv.org/abs/2412.03685) treats a sequence as
video with a pose guider, reference network and motion module, but its pilot
fine-tuned on only 619 pairs and still reported unmet subject-consistency
expectations. [One-to-All Animation](https://arxiv.org/abs/2511.22940) likewise
separates identity and pose control, but is a new trained video framework, not a
validated drop-in for exact pixel sprites. ComfyUI's own
[ControlNet examples](https://github.com/comfyanonymous/ComfyUI_examples/tree/master/controlnet)
stress that each control model requires the correct representation. These
findings support the present split: deterministic animation structure first,
learned appearance second; temporal generation and training remain later gates.

The user's 2026-09-09 direction is to resolve motion, foot attachment and
missing artwork. Head flicker is deferred. Compare an established biped rig
and motion source with the current tracing, and test rough completion before
generation. This is a proposed refinement of the approved experiments; review
the new actual inputs before inference. The accepted twelve-pose sheet remains
the current control until an alternative is reviewed.

Implementation started 2026-09-09. The
[prepared input review](../../experiments/character_binding/evidence/run-gap-inputs-v1/review.html)
contains the official Spineboy run benchmark, cuff annotations, twelve prefill
comparisons and masks. Prepared ComfyUI graphs preserve the encoded prefill and
use an actual grayscale-derived sampler mask. The user subsequently authorized
continued experiments: twelve connection-control requests have now completed.
[Results and new boot-guide review](../../experiments/character_binding/evidence/run-gap-control-v1/review.html)
show that the prefill supplies most of the connection; restricted generation
adds little at 48px. No complete-cycle batch was run. The new guides allow
changing boot surfaces and full-leg redraws. The user approved those views with
stouter legs/boots; v2 implements the requested proportions and four full-leg
redraws are now complete. [Results and compositing comparison](../../experiments/character_binding/evidence/stout-boot-redraw-v1/review.html).
The source guide supplies pose/volume, the original mouse supplies appearance,
and the mask describes the editable region. Surface-ID colors are an inspection
view only. Results retain broad poses but still move local geometry; the narrow
mask can also clip a newly drawn boot. Keep the later broader composite clearly
separate from the actual mask sent to the provider. A full cycle is not yet
generated or accepted.

**Current defect: leg ownership at the crossing.** The user identified that
pose 10 assigns the foreground folded knee to the standing leg. Keeping one
boot high and one low did not establish correct anatomy. Both trouser legs
share the same brown guide colors and yellow surface label; the correct draw
order becomes ambiguous in the flattened picture. The
[complete-leg diagnostic](../../experiments/character_binding/evidence/leg-ownership-review-v1/README.md)
separates the two chains and shows the intended overlap.

The completed E1 refinement on pose 10 compared one whole-frame redraw with an
explicit A/B leg-identity and occlusion guide against two independently
completed leg layers. Compose the standing far leg, folded near leg and coat
in known order. The latter is favored because the compositor owns the overlap;
the model still must follow each isolated leg's geometry. The labeled guide is
only an ordinary image reference, not a trained semantic ControlNet signal.
Use one candidate per approach/part initially (three generated images total),
after review of their actual inputs. Verify knee→calf→boot assignment and
registration in the isolated layers and final composite. Then check pose 4,
where the folded/standing roles reverse, before expanding to the full cycle.
Keep the successful style and stoutness. E2 remains a fallback for deformation
of valid parts, E4 for stronger local conditioning if needed, and E5 for later
sequence consistency; none should obscure a known incorrect limb connection.

The user approved this comparison and it is now complete: three requests for
pose 10 plus one combined-guide pose-4 follow-up. The labeled-guide package
improves the apparent crossing; independent layers make overlap ownership
deterministic but the folded boot shifts about 11 working pixels downward.
The standing leg stays much closer to its guide. Preserve that raw result and
calibrate part-to-rig attachment explicitly before reusing it. The combined
route was the better visual candidate in this limited trial; the user has
since selected independent layers. Neither result proves pose preservation.
[Results and exact prompts](../../experiments/character_binding/evidence/leg-ownership-trial-v1/README.md).

Use an existing authoring rig for the independent comparison rather than
expanding the custom editor. Retain Zebes' editable pose representation and
import sampled joints through an adapter. A template must still be fitted to
the mouse, and a rig template does not itself provide a run animation. Compare
the supplied tracing with one identified, reusable run clip, sampled at twelve
distinct phases without duplicating the closing endpoint. First inspect the
plain skeleton and filled limb silhouettes; then attach the mouse artwork.

Treat geometry and missing artwork separately. Fix an incorrectly placed cuff
or sole through binding. Complete absent trouser/limb surfaces with artwork,
including generated artwork. Prefer completing reusable parts once where
possible, with overlap under the coat and boots. C++ should place and deform
those parts; it does not need to synthesize their final painted texture.

### Boot views and lower-leg redraws

The current one-image-per-boot representation is insufficient for the intended
view. On 2026-09-09 the user identified that the leading boot in pose 1 shows
its underside while the trailing boot does not. Reusing these painted views
through all twelve phases preserves the wrong visible surfaces. Better joints
or a filled calf gap cannot correct that. The user considers the connection
prefill worth testing, without accepting the cuff estimates or rigid boot art
as finished animation.

Keep near/far limb identity separate from leading/trailing position: each leg
changes its screen position through the cycle. Sole visibility depends on foot
orientation relative to the camera, not simply which foot is farther forward.
The source suggests an oblique view, while the human pose sheet primarily
supplies side-view joint positions. That sheet does not supply the mouse's
foot thickness, surface orientation or foreshortening.

Use pose-dependent replacement artwork for the boots, and for the lower legs
where a bend or overlap changes their visible shape. Start by defining the
fixed camera and reviewing four lower-body key situations: forward extension,
ground support, toe-off, and tucked recovery/passing. These are review cases,
not an assumption that exactly four drawings will cover both feet. Track ankle,
heel, toe/sole contact and cuff anchors alongside the intended visible surfaces.
Small in-between changes can reuse/deform a compatible drawing; view changes
need another drawing. Author a twelve-pose view assignment before expanding
the finished art kit.

The revised generative comparison should permit changes to the whole boot,
cuff and adjoining calf, with enough silhouette margin for the intended view.
Preserve the reviewed contact/pose targets and unrelated character regions.
Do not preserve boot pixel identity as an acceptance condition for this branch.
Generate or draw the connected lower-leg unit in difficult key poses, then
separate reusable boot and trouser artwork with overlap where useful. Review
the proposed silhouettes and surface-view guides before this new generation
setup. A simple 3D boot/calf proxy is an optional way to obtain consistent
perspective and genuine depth, without replacing the entire mouse with a 3D
model. Ordinary 2D warping cannot reveal a hidden painted surface.

The existing `run-gap-inputs-v1` masks remain a limited connection control.
They protect most boot pixels and therefore cannot establish whether a model
can redraw the required boot views. Do not interpret their outcome as a verdict
on whole-leg/boot generation. Implement the first replacement drawings as
offline corrective layers; a production attachment format is not yet needed.

Reference inspection: the visible frame rows in
[Slynyrd's run-cycle sheet](https://www.slynyrd.com/blog/2018/8/19/pixelblog-8-intro-to-animation)
show distinct foot silhouettes for the different phases. His more detailed
[clothed walk sheet](https://www.slynyrd.com/blog/2024/5/24/pixelblog-50-human-walk-cycle)
shows changing shoe contours and trouser folds; the accompanying description
explicitly adjusts the opposite half-cycle for implied perspective. These are
visual/construction references, not a replacement run or training dataset.
[Spine attachment keys](https://us.esotericsoftware.com/spine-attachments)
provide an established mechanism for selecting different drawings on a bone.

## Future direction: resolution and equipment layers

Recorded 2026-09-09; this is not the current experiment's implementation scope.

The user also requested future coat/cloak and cape motion, while keeping it
out of the crossed-leg experiment. Give the hem a resting shape and a neutral
running shape, lift it where knees/thighs actually contact it, and let it settle
with lag rather than snapping to each leg angle. A moving character can retain
some flare even when no knee is underneath. Separate upper-coat anchoring from
front/back hem shapes; the current rigid body image alone cannot express this.
A removable cape should attach to the shoulders/back and have separate trailing
and gravity/settling motion. Its response may be authored or simulated as an
offline aid, then baked into the twelve frames with a clean loop boundary.
Complete underlying leg/body art prevents newly exposed areas from becoming
gaps. No garment physics or new runtime format is required for the present trial.
The intended pipeline must produce a defined logical sprite resolution and
pixel grid, with explicit registration and reproducible processing of whatever
native canvas a generator returns. A requested canvas size alone is not a
resolution contract.

The character must eventually support adding and removing pauldrons, cloaks,
swords, tiaras and other equipment. Keep a complete underlying character and
poseable equipment/clothing artwork, with shared rig attachments, origins,
timing and explicit front/back draw order. Removing clothing needs valid art
underneath it. Whole-character redraws can be useful references, but baking all
equipment into each independently generated frame is insufficient for that
goal. Continue the run-animation experiments first, carrying this direction
into later pipeline choices.

## 1. What was reviewed

- [Current handoff](../handoff.md), the relevant Track 5 roadmap and
  [authoring boundaries](../architecture/editor-and-authoring.md).
- The entire [character-binding findings and 41-entry decision ledger](../../experiments/character_binding/FINDINGS.md),
  including the later corrections to earlier conclusions.
- [Coherent-sheet gate](animation-sheet-gate-2026-08-29.md),
  [independent-frame pilots](../history/animation-pose-conditioned-experiment.md),
  and [Codex skeleton-conditioning closeout](../history/codex-pose-conditioning-2026-09-06.md).
- [Layer deformation diagnosis and fallback proposals](../character-layer-deformation-experiment.md),
  the current puppet README, skeleton/puppet interfaces, and all three tracked
  puppet documents.
- Representative retained images: the standing source, isolated running source,
  twelve-pose skeleton sheet, and a rejected high-recovery provider output.
  Historical numerical results below are recorded results, not rerun metrics.
  Some older generated output and runners were deleted during cleanup.
- Primary documentation and repositories linked in section 4.

Read-only inspection also confirmed derry's live software inventory. No model
was downloaded, environment changed, service restarted, render queued, or
production asset modified.

## 2. What the experiments establish

The ledger numbers refer to FINDINGS.md. Together these rows cover its entire
ledger, plus the preceding August experiments.

| Experiment family | Recorded result | Interpretation for this retry |
|---|---|---|
| August coherent idle/run sheets | First attempts failed extraction; second run passed processing but failed live motion. Idle visibly oscillated about three native pixels. | Sheet consistency and valid extraction do not establish ordered gait or stable registration. |
| August independent poses, composite identity and separated identity views | Six attempted provider turns included a transport failure, a canvas mismatch, and completed pairs rejected for body/helmet/waist changes and ambiguous opposing phases. | Separate plumbing failures from art failures. More identity views did not solve the completed pairs. |
| 1–3: depth, then depth with IP-Adapter | Weak depth: 337% measured head drift. Strong depth: 33.8% and mannequin-like art. IP-Adapter: 11/12 heads within 1.3%, but a large outlier and missing facial identity. | Strong structural conditioning can preserve dimensions while destroying the identifying details absent from the proxy. Fixed seed is not an identity constraint. |
| 4–7: native-size review, pixel LoRA, Canny, unconstrained generation | Native review exposed unreadable painterly output. Canny plus pixel LoRA improved profiles and local pose differences. Unconstrained images gave the best reference art. | Style and pose need separate evaluation. The successful local Canny examples were not twelve-frame evidence. |
| 8–10: primitive fitting, silhouette binding, deterministic extraction | Complete primitive fit 68.8% IoU; profile bind 97.3% neutral IoU but missing hidden limbs. C++ extraction/topology passed focused cases. | A close neutral silhouette does not supply concealed legs, limb identity, or depth order. |
| 11–13: four-pose Canny, ordinal depth, dual controls | Identity survived weak controls, but flight was grounded or facing changed. Strong depth improved elevation while damaging style. Dual controls repeated the failure. | Do not reopen the same strength sweep. Test a different input representation or temporal mechanism. |
| 14: direct C++ deformation | Exact neutral and complete sampling, but coat/boots intersected and ownership was wrong. | The mapping implementation passed a narrow test; the input decomposition failed. |
| 15–18: low-poly 3D, reference fitting, shared families, authored Blender mouse | Pose, identity, reusable species builders, import and runtime worked. Reference-fit silhouette reached 75.5%. Final art direction rejected the primitive style. | 3D animation was not disproved. The particular modeling/rendering approach failed the desired appearance. A 2D textured Blender rig would test a different mechanism. |
| 19–22: explicit layers, See-through, skinned arm, exclusive ownership | Layered direction passed; rigid seams remained. See-through supplied useful sleeves/boots/coat but missed legs/tail and hallucinated human anatomy. Isolated-arm success concealed composite ghosts. | Build the whole character. Semantic segmentation, hidden-surface completion, ownership and skinning are separate tasks. |
| 23–28: mesh diagnosis, angle blending, trimming, ownership and shoulder correction | Diagnosed 105 stuck pixels, 876 unbacked pixels and folds. Some fixes improved shapes despite reducing arm area. Corrected shoulder placement exposed other defects. | Freeze geometry and ownership before comparing solvers. Area preservation is not a visual-quality score. |
| 29–32: stretched backfill, 48px review, coat relationship, immutable coat | Filling the entire arm footprint enlarged a correct coat. Immutable generated coat preserved its digest and alpha. | Complete surfaces that actually exist behind a limb; some newly exposed areas should be transparent background. |
| 33–34: native motion audit | Versions differed by only 5–21 pixels at 48px; contact/passing silhouette changes were 11/13 of 717 pixels. Only one arm drove artwork. | Those semantic-arm variants were not full-body gait experiments. Tiny high-resolution diagnostic improvements had little shipped benefit. |
| 35–37: Codex pose sheets and registration | Model ignored exact canvas/palette requests. Tooling could enforce them, but per-figure normalization removed real airborne motion. | Retain raw outputs. Use shared world coordinates and a fixed scale; never seat every generated frame on its own bottom row. |
| 38: matched image/skeleton local edit | Horizontal foot separation followed a small requested change; vertical travel undershot. | Limited evidence of local pose sensitivity, not proof of arbitrary skeletal control. |
| 39–40: matched and simplified four-reference pilots | Both included a running reference contrary to the intended two-input test; both converged on generic strides. | Useful failure evidence, but invalid as tests of the requested input contract. |
| 41: corrected standing-source plus skeleton | All three outputs still gave generic split strides; recorded height spread 17.7%, baseline spread 74px in the wrong phase order. | Valid evidence against this reference-only mechanism; do not spend another full batch on it unchanged. |

The most plausible causes are incomplete conditioning, conflicting appearance
and geometry signals, missing hidden artwork, and evaluation that sometimes
measured a proxy for the desired behavior. They are not all model failures.

Two interpretations need qualification:

- The repeated generic strides are **consistent with** a strong learned running
  prior and weak skeleton interpretation. They do not prove that a model can
  never represent the requested poses. The recorded tests cannot isolate the
  model's internal cause.
- Some corrected Canny guides still contained thick bones and joint dots, and
  depth inputs used authored ordinal regions. These differ from ordinary image
  edges and estimated depth. Distribution mismatch is a plausible additional
  cause, not established causality. ControlNet's reference implementation treats
  edges, depth and human pose as distinct trained conditions. Even a dedicated
  ControlNet is learned conditioning, not a geometric constraint.
  [ControlNet implementation](https://github.com/lllyasviel/ControlNet).

The current snapshot also supersedes several historical status statements:

- `mouse_interactive_run_v1.json` now has five parts, twelve distinct poses,
  both arms and both legs bound; its head and tail remain in the body part.
- `test-puppet.json` has ten parts, 23 joints, 22 bones and twelve distinct
  poses. Inspection found essentially exact bind-relative bone lengths. The
  interactive document's largest relative length difference is about 0.0025%.
  Current bone stretch is therefore not an established cause of its appearance.
- The six old layered-render hard gates became review notices on September 7.
  Older claims that the same render remains blocked by hard validation are stale.
  This plan evaluates visible defects without silently reinstating those gates.

## 3. Available equipment and starting assets

Live inspection on 2026-09-08 confirmed:

| Resource | Status |
|---|---|
| derry | Reachable through SSH; RTX 3090, 24,576 MiB VRAM; about 64 GiB system RAM |
| ComfyUI | Running at loopback port 8188, version 0.34.0; Python 3.12.3; PyTorch 2.13.0+cu130 |
| Image checkpoints | SDXL base 1.0 |
| Conditioning | xinsir SDXL Canny and depth; IP-Adapter and IP-Adapter Plus SDXL; `ComfyUI_IPAdapter_plus` custom nodes |
| Style | `pixel-art-xl` LoRA |
| Video | No video weights in the inspected `diffusion_models` directory; no AnimateDiff/Wan wrapper in the inspected custom-node directory |
| Blender | 4.0.2 installed at `/usr/bin/blender` |
| See-through | Prior 3090 execution and retained layers are documented; its disposable environment is not verified as still available |

The inventory covers the normal ComfyUI directories and documented setup, not
every storage location on the machine. A model file existing does not by itself
verify that a new workflow runs.

Use the previously selected
[isolated running mouse](../../experiments/character_binding/inputs/interactive-run-source-v1.png)
as the proposed primary art source: its limbs are more exposed and the current
interactive puppet was built against it. Keep the
[standing source](../../experiments/character_binding/inputs/profile-binding-deformation-v2/source-color.png)
and its See-through layers as a separate reference kit. The two images differ
in proportions and facing; do not silently combine them as interchangeable
pixel sources. Any reused layer needs explicit fitting and art acceptance.

Use the [user's twelve-pose sheet](../../experiments/character_binding/inputs/run-pose-reference-12.png)
and [its explicit joint traces](../../experiments/character_binding/inputs/run-pose-trace-v1.json)
as the motion starting point. Rig Bench and its generated tracing underlays
are comparison evidence only.

## 4. Skeleton, pose and image-binding options

The roles matter: a pose estimator locates joints; a rig defines a hierarchy;
IK solves joint positions; skinning moves pixels; completion supplies missing
art. No single one of these operations performs all the others.

| Tool or library | Relevant capability | Recommendation |
|---|---|---|
| Existing Zebes `skeleton_rig`, puppet document and renderer | Authored joints, chains, twelve poses, part ownership, rigid/two-bone mesh transforms, deterministic export | First baseline. Reuse existing tools and correct the part kit before building another editor. |
| [Blender armatures](https://docs.blender.org/manual/en/latest/modeling/modifiers/deform/armature.html) | Mesh deformation with bone weights and optional volume preservation | Best already-installed independent binding comparison: textured 2D part meshes, orthographic camera, explicit weights and corrective shapes. Version-check features against installed 4.0.2. |
| [Spine mesh attachments](https://en.esotericsoftware.com/spine-meshes), [weights](https://us.esotericsoftware.com/spine-weights), [IK](https://esotericsoftware.com/spine-ik-constraints) | Image meshes, bone binding, deform keys and one/two-bone IK | Strong purpose-built authoring alternative. Mesh functionality requires Professional. Useful if manual rig editing is the bottleneck; no purchase or runtime integration is proposed. |
| [Moho](https://moho.lostmarble.com/pages/features) | Bitmap/PSD rigging, meshes, FK/IK and authored Smart Bone corrections | Strong alternative for an artist finishing the cycle; adds a new authoring workflow. |
| [Live2D](https://docs.live2d.com/en/cubism-editor-manual/deformpath/) | ArtMesh deformation and authored deform paths | Useful for face, cloth and soft shape corrections; my preference for this whole-body gait is skeletal cutout tooling. |
| [DragonBones](https://dragonbones.github.io/en/animation.html) | 2D skeletons, weighted meshes and layered-image import | Relevant functionality, but deployment/maintenance suitability has not been validated here. No reason to migrate for this first trial. |
| [libigl](https://libigl.github.io/tutorial/) and [bounded biharmonic weights](https://igl.ethz.ch/projects/bbw/) | Geometry-aware weight computation and deformation algorithms for 2D/3D shapes | Candidate for an isolated solver comparison if a valid part visibly deforms badly. Computes weights, not missing anatomy or gait. Keep outside the engine initially. |
| [Rigid MLS](https://people.engr.tamu.edu/schaefer/research/mls.pdf) | Fast point/line-handle image deformation | A bounded fallback, not the first work item. The paper explicitly permits foldbacks; the existing note saying it cannot fold is incorrect. |
| [Animated Drawings](https://github.com/facebookresearch/AnimatedDrawings) | Image rigging and BVH motion retargeting; manually configurable skeletons | Useful reference implementation. Human-like assumptions and missing concealed artwork remain relevant; repository archived September 2025. Lower priority than the installed Blender route. |
| [DWPose](https://github.com/IDEA-Research/DWPose) / [MMPose](https://github.com/open-mmlab/mmpose) | Pose estimation from images/video | Optional help extracting a motion reference. Do not use a human estimator as ground truth for this mouse or as an image-binding library. We already have authored joints. |
| [See-through](https://github.com/shitagaki-lab/see-through) | Generated semantic layers and concealed-surface candidates | Reuse accepted layers selectively. Its own documentation distinguishes decomposition from rigging and artistic layer design. No whole-character automatic acceptance. |
| [AnimateDiff Evolved](https://github.com/Kosinkadink/ComfyUI-AnimateDiff-Evolved) | Temporal sampling with ControlNet/IP-Adapter integration | A possible later temporal A/B. Model families must match: existing SDXL assets cannot simply accompany an SD1.5 motion module. SDXL support has its own qualifications. |
| [Wan VACE](https://github.com/ali-vilab/VACE) / [ComfyUI integration](https://docs.comfy.org/tutorials/video/wan/vace) | Reference-conditioned, controlled and masked video generation | Preferred optional temporal experiment. New mechanism relative to the recorded still-image tests; no promise of exact pose, pixel style or loop closure. |
| [ToonCrafter](https://huggingface.co/Doubiiu/ToonCrafter/blob/main/README.md) / [RIFE](https://github.com/hzwer/ECCV2022-RIFE) | Cartoon interpolation / intermediate-flow interpolation | Defer. Neither establishes twelve specified poses from missing or incorrect keyframes. ToonCrafter's published model uses sixteen frames and notes VAE flicker. Interpolation is useful only after sound keys exist. |

### Established rigs and relevant external experiments

[Spineboy](https://en.esotericsoftware.com/spine-examples-spineboy) is the most
direct 2D reference for the independent rig comparison: it has a published run
animation and separate leg and foot IK controls, with foot targets independent
of the hip. Use it to compare foot/contact behavior, not as a claim that its
artwork or proportions fit the mouse automatically. Blender's
[Rigify Basic Human template](https://docs.blender.org/manual/en/latest/addons/rigging/rigify/basics.html)
is the available free rig starting point; its documented workflow explicitly
requires fitting bones to the character. Check the installed Blender version
and Rigify availability before selecting that route. A
[Mixamo](https://helpx.adobe.com/creative-cloud/faq/mixamo-faq.html) animation can
supply a separate motion reference, but its humanoid auto-rigger does not bind
our PNG and is sensitive to unusual proportions, tails and clothing.

[OpenPose COCO/BODY_25](https://github.com/CMU-Perceptual-Computing-Lab/openpose/blob/master/doc/02_output.md)
defines keypoint conventions; BODY_25 includes heels and toes. This is useful
for interchange and foot landmarks, not a skinning rig. A learned pose-control
model needs the particular joint layout and rendered convention it expects.
Export that representation from the authored mouse rig instead of replacing
tail, coat and equipment controls with a human pose-estimation schema.

[Sprite Sheet Diffusion](https://arxiv.org/html/2412.03685v2) addresses almost the
same task. It fine-tunes an Animate Anyone variant using reference appearance,
pose guidance and temporal layers. Its generic ControlNet/IP-Adapter baseline
often produced stylistic similarity without matching the actual character.
The authors still report difficulties with props and fine details. Their
[repository](https://github.com/chenganhsieh/Sprite-Sheet-Diffusion) links code
and pretrained weights; local inference, twelve-frame handling and the
green-coated mouse are unverified. This merits a later reproducibility pilot,
not a promise that an unmodified video model solves our run. The paper's first
training stage used over 30 GB VRAM; that is not an inference-memory estimate.

A [2025 Surrey experiment](https://marcovolino.github.io/docs/papers/2025-wong-cvmp.pdf)
found better results generating human motion before deterministic pixelation
than generating from an already pixelated portrait. Its small human dataset
does not establish the best pipeline for this mouse or for authored pixel art.
It supports keeping generation resolution and final pixel-grid conversion
separate experimental choices.

[Spine's mix-and-match example](https://en.esotericsoftware.com/spine-examples-mix-and-match)
demonstrates separately selectable clothing/accessories and prepared limb art.
This is a concrete precedent for the longer-term equipment requirement.
The current layered direction remains plausible; repeatedly repairing every
posed frame should not substitute for completing a reusable underlying kit.

Do not begin custom LoRA training from one reference image. The missing signal
is a verified variety of poses and hidden surfaces; training on the failed
outputs risks preserving their errors. A character LoRA could be reconsidered
after this process supplies an accepted multi-pose dataset.

The user reiterated post-training as a possible later route on 2026-09-09.
Keep exact pose/image/mask pairs and provenance now, but label procedural
prefills and unreviewed outputs as inputs, not correct targets. Define a
specific failure to train against: character appearance, missing-part
completion, pose control, or sequence consistency. These need different
supervision; appearance LoRA training alone is not an established solution to
pose obedience. Evaluate on held-out poses or entire sequences before accepting
a trained model. Do not postpone the current input and binding experiments
while collecting a speculative large training set.

## 5. Shared experiment contract

Every branch uses one frozen identity kit, one motion specification, one fixed
camera/scale and one review procedure. Changes create a new named revision.

**Output.** Exactly twelve distinct, ordered RGBA frames of one complete
right-facing cycle; fixed canvas, origin and ground datum. Native review begins
at 48×48, matching earlier production evidence, with enlarged nearest-neighbor
views and a Catacombs background. If the entire silhouette cannot fit at the
chosen scale, widen the canvas consistently before freezing it.

**Timing.** Retain the files' existing 8 fps as a slow diagnostic view. Compare
12 and 15 fps during the motion-only setup, then freeze the preferred cadence
for all art comparisons. Fifteen fps gives a 0.8-second twelve-frame cycle and
matches the historical four-ticks-per-frame reference at 60 Hz. This is preview
authoring, not permission to alter production timing. At integration, reconcile
the stride with actual world speed and render scale explicitly.

**Motion.** Label near/far limbs on an annotated source; do not infer them from
`_l`/`_r`. Annotate support foot, sole/toe anchors, flight intervals, root height,
limb order and intended limb crossings for all twelve frames. Preserve bone
lengths for the planar baseline. If a pose requires foreshortening or a new
visible surface, author a substitute drawing rather than stretching a limb.

**Registration.** A single transform maps the whole kit to output coordinates.
No per-frame bounding-box scale, bottom-row seating, automatic height matching,
or phase reordering. A raw generator result that violates registration is scored
as such. Any explicitly proposed registration correction is retained and scored
separately; it must not remove intended body rise or flight.

**Evaluation.** The original numerical screens below are retained as diagnostic
references, not gates. The current priority is visibly correct binding and
motion. Review actual inputs with the user before generation, and judge the
complete animation visually instead of reinstating the old prerequisite chains.

| Property | Evidence and proposed screen |
|---|---|
| Pose obedience | Manually identifiable wrists, knees, soles/toes and pelvis against target overlays. Mean error ≤3% and maximum ≤6% of fixed reference body height; approximately 1.3/2.6 pixels for a 44px-tall sprite. Mark concealed joints unobservable instead of guessing. Correct support/flight phase is required independently. |
| Grounding | Support sole within one native pixel of its authored ground/contact target. Flight frames preserve their authored clearance. Do not compare an ankle directly with the ground. |
| Identity | Same face, ears, muzzle, coat, belt, scarf, hands and boots. For rigid landmark comparisons, measure in local part coordinates; investigate >5% unrequested proportion change. Constant source digests establish asset reuse but do not alone establish good rendered anatomy. |
| Whole-body motion | Part-tint playback proves both arms and both legs follow their assigned tracks. Head/torso rise and tail motion are deliberate. Twelve different file digests alone cannot pass this screen. |
| Foot sliding | During support, compare foot displacement with the intended root/world translation. In a treadmill preview the planted foot moves backward in sprite coordinates; a fixed local foot is not the correct universal target. |
| Loop closure | Review 11→0 at normal speed and frame-step across it. Compare pose/velocity discontinuity with the authored control and ordinary adjacent intervals. Do not demand that frames 11 and 0 be identical or append a repeated first frame. |
| Flicker | Inspect motion-compensated face/coat regions and fixed palette behavior. Pixel differences caused by intended articulation are not identity drift. A global similarity score is secondary evidence only. |
| Composite defects | Full-character and hidden-part/tint views at 48px: no visible duplicate limb, torn shoulder, boot swap or displaced coat patch. Keep high-resolution hole/fold counts diagnostic unless a visible failure or invalid geometry justifies a gate. |
| Final acceptance | Side-by-side loops with method names hidden, native/enlarged and on Catacombs. Score identity, gait, continuity, pixel readability and artifacts separately; one unacceptable category cannot be averaged away. |

Validate the measuring tools with a known static duplicate cycle, a deliberately
phase-swapped cycle, and a deliberately shifted frame. Also measure the
deterministic guides: they cannot acquire anatomical drift by construction.

## 6. Experiments and execution order

### E0 — repair source calibration and establish the controls

No inference. Audit current documents and render the existing twelve-frame
puppets through the current tools. Keep both as baselines; do not edit their
tracked originals. Produce a skeleton/solid-part loop with visible foot contacts
and limb colors, plus actual artwork playback.

Check all chains against the selected source and current render, including the
head and tail that the five-part puppet leaves in the body. Use the existing
near-constant bone lengths as a starting point; do not invent a stretch fix.
Resolve any incorrect pose, phase, attachment, ground datum or drawing order in
the copied trial document. A silhouette can demonstrate gait without final
texture, so settle motion before pursuing surface quality.

Deliver one reviewable kit: source digests, twelve poses and phase labels, part
inventory, shared transform, timing, reference loop and evaluation thresholds.
If the control does not read as a run, spend the effort on motion authoring;
image generation cannot validate an incorrect motion target. Show this kit to
the user and wait for their input review before generation. The first repair
used `mouse_run_calibrated_v1.json`: near/far arm tracks corrected against the
leg phase, source joints retraced, rigid boots split from the legs, and
`retarget_frames` matching `reference_01` to the source drawing exactly.
The user then rejected its inherited gait. `mouse_run_reference_v2.json`
instead takes the supplied sheet's limb angles directly and retains the
source drawing as the separate bind pose. A shared scale for traced legs and
explicit support/flight registration are part of this new input review.

### E1 — complete layered puppet with reusable corrective artwork

**Highest expected chance of a usable final cycle.** It preserves identity
structurally and gives every required pose an explicit source.

Finish the running-source part kit: rigid face/head with original ears; torso;
two complete arms with hands; two complete legs with boots; independently owned
tail; and coat/scarf pieces only where their movement is visible at 48px.
Remove moving-part pixels from static artwork and complete actual surfaces
revealed behind them. A stable coat texture can move with the torso without
being repainted or stretched into an old arm footprint.

Start with available pixels and accepted compatible completion. If a required
surface is absent, allow up to **six static part-completion image requests**:
at most two candidates for each of three explicitly missing part groups. These
generate reusable drawings, not six independently styled animation frames.
Review them at native size and in the extreme poses. Keep original visible
identity pixels authoritative. Record which parts were authored or generated.

Use existing rigid/angle-blended deformation. Add up to four explicit corrective
drawings for the bends or limb overlaps that remain visibly wrong. Replacement
attachments are allowed in this branch and must be recorded; inspect their
switches during playback. Do not force one texture to represent a hidden side
of a boot that it does not contain.

Render all twelve frames. Permit one diagnosed revision of part ownership or
corrective drawings, then review the full cycle. If completion fails its bounded
budget, report the exact missing drawings and continue independent comparisons
with the diagnostic kit; do not call a flat placeholder finished art.

### E2 — independent image binding in Blender

**Medium-high chance if E1's remaining defect is deformation.** This separates
limitations of the current renderer from limitations of the artwork and pose.

Use the same frozen RGBA parts and target joint positions in Blender 4.0.2.
Build textured planar meshes with explicit weights, a fixed orthographic camera
and unlit materials. Keep the head rigid. Use layer depth for occlusion and
author corrective shapes only where required. This is a cutout mouse, not a
rerun of the rejected primitive 3D mouse.

Compare current rendering and Blender on four stress poses: opposite contacts,
a tightly crossed passing pose, and an airborne/recovery pose. Select exact
frame indices from E0's annotations before rendering. Match rasterization and
native scaling; a filtering change must not be credited as better skinning.

If the full composites visibly improve, render all twelve with the same kit.
If they do not, stop after the four-pose A/B. Time-box the independent setup to
one working day. MLS/BBW/ARAP are deferred unless this test identifies a specific
shape defect that cannot be corrected more cheaply with a drawing.

### E3 — Codex cleanup of already posed artwork

**Medium chance as a finishing step; low confidence as a whole-frame redraw.**
This tests a different input from the failed stick-skeleton requests.

Supply the actual posed RGBA composite as the edit target, the frozen identity
reference, and clearly bounded defect regions. Describe the local repair, not
an instruction to invent a running pose. No previous generated frame becomes
the next frame's identity source.

Pilot the four E2 stress poses, one request per pose. Retain untouched provider
outputs and provenance. The current image tool may not expose an exact numeric
inpaint mask or reproducible seed: never imply that a prose mask is enforced.
After generation, deterministic compositing may admit only pixels inside the
predeclared repair regions. The region allowance can include a small silhouette
collar; it is fixed before seeing the output.

The model's raw pose/identity score and the masked composite's score are
reported separately. Preserve the input outside the masks byte-for-byte.
If a response changes canvas or shifts the local artwork so that the edit cannot
be mapped reliably, reject it; do not conceal the failure with per-frame fitting.

If all four repairs improve the native composite without moving landmarks or
introducing style mismatch, run one fresh twelve-frame batch with the same
policy, one candidate per frame. Otherwise stop. Maximum **16 frame-edit
requests**, in addition to E1's possible six part requests. No blind rerolls or
mixing pilot frames into a claimed complete batch.

### E4 — ComfyUI masked img2img from the posed puppet

**Medium chance for small repairs.** Uses installed models and changes the
mechanism from generating a mouse out of a sparse control map.

**Connection-control result, 2026-09-09:** the twelve initial images completed
in approximately 148 seconds of summed request wall time. At 48px, output
differs from its prepared input in 3–11 pixels; most useful gap closure comes
from the prefill. Blank inputs retain gaps and stronger denoise can reopen
small light regions in filled inputs. No setting justified a full-cycle batch
for the current boot-perspective problem. Four reserved checks and the
twelve-image complete-cycle allowance were not used. See
[retained evidence](../../experiments/character_binding/evidence/run-gap-control-v1/README.md).
The next input review is the whole-leg/boot guide described above, not another
weight sweep on this frozen mask.

**Proposed revision after the 2026-09-09 discussion:** compare actual image
prefills before sweeping conditioning weights. The E3 preparation script drew
bone-aligned missing-leg regions into the repair mask, but left the corresponding
image pixels unchanged. Its region guide was an ordinary reference image, not
an inpainting-mask input. Those results do not test painted gap completion.

For frames 5 and 10, prepare three inputs on the same fixed canvas: unchanged
broken puppet; surrounding-color/blur fill in the gap; and a rough trouser
shape connecting the correct knee/calf to the boot cuff. The shaped fill uses
the intended contour and local material colors. Blur alone does not specify
which limb owns the missing pixels. Show these inputs, the joint/cuff/sole
overlay and the exact masks before inference. Include a narrow editable cuff
transition; the old complete-boot protection plus collar may prevent useful
seam changes. Keep the sole and foot placement protected.

This follows a documented painting workflow:
[Krita AI Diffusion](https://docs.interstice.cloud/selections/) supports blur,
border-color and hand-painted prefill, plus separate denoising and blend masks.
Use installed SDXL base, pixel-art-xl and IP-Adapter with one fixed identity
for the first comparison. Use a real sampler noise mask and preserve the
prefill in the encoded image. Check the installed graph implementation:
upstream ComfyUI's
[`VAEEncodeForInpaint`](https://github.com/comfyanonymous/ComfyUI/blob/master/nodes.py)
replaces the masked image with neutral gray before encoding. Ordinary VAE
encoding followed by `SetLatentNoiseMask` is the proposed base-model path for
testing retained prefill. A dedicated inpainting model is a separate comparison.

Keep edge/depth control off initially to isolate the prefill. In particular,
edges extracted from the broken silhouette could encourage preservation of
the defect. If introduced later, show actual edges from the intended completed
shape and use the matching checkpoint; do not use a colored skeleton as Canny.

Pilot allocation remains **16 images maximum**: two poses × three inputs ×
denoise **0.35/0.60** × one fixed seed = twelve images. Repeat the selected
input/strength on both poses with a second seed, then check pose 11 with both
seeds, for four more images. These strengths are hypotheses, not established
optima. Hold prompt, sampler, steps, LoRA/IP-Adapter settings and masks fixed.
If neither strength improves the local anatomy, retain the failure instead of
choosing a nominal winner. Record the actual graph and all settings.

Apply the same outside-mask copy and separate raw/final scoring as E3; a VAE
round trip can change supposedly protected pixels. Include the unchanged puppet
as a control so doing almost nothing cannot win the repair comparison.

If a setting passes, run one complete twelve-frame batch at that setting and
one predeclared seed policy. Maximum **28 images** including the pilots. Compare
the resulting loop with E1/E2 and E3. Stop if noise reduction preserves defects
while more noise introduces drift; do not extend this into another weight sweep.

### E5 — optional temporal video trial with Wan VACE

**Best untested generative mechanism in this shortlist, but lower confidence
than E1 for an exact twelve-frame pixel-art loop.** Temporal processing may
reduce independent-frame drift; it does not guarantee contact states or a
seamless cycle. This is an optional add-on to the recommended first round.

Use the accepted posed puppet sequence as motion-bearing video and the same
identity source. Follow an official VACE reference/control or masked-edit
template, verifying that every input path actually reaches the sampler. If the
selected path expects depth or edges, supply the matching representation rather
than pretending RGBA is depth. Use the masked-video path for colored cleanup.

Download compatible weights only after this branch is approved. First profile
VACE 1.3B with a short 480-class, 49-frame input on the 3090, using offload as
needed. It is a setup/conditioning check, not a verdict on 14B quality. If
resources permit, use a documented quantized/offloaded 14B configuration for
the quality comparison. The full 14B file alone is around 32 GB, so native full
GPU residency is not a credible 24 GB plan. Quantization support is available
through [WanVideoWrapper](https://github.com/kijai/ComfyUI-WanVideoWrapper);
prefer native nodes where the verified workflow permits them.

Keep new environments isolated from the working ComfyUI installation. Do not
upgrade shared packages to satisfy an optional trial. Record peak VRAM, host
RAM, wall time, model hashes and exact graph. Weight file size is not a VRAM
benchmark; actual fit is an open question until profiled.

The proposed 49-frame control contains two cycles at two samples per each of
the twelve phases, plus the final endpoint. Intermediate controls come from
the E0 skeleton, not optical interpolation of pixels. Export exactly the
predeclared second-cycle indices **24, 26, …, 46** as the twelve-frame candidate.
Compare phase-equivalent frames across both cycles and endpoint 48 for drift
and closure. The model's frame-count rules are handled in the control input;
no authored phase is dropped and no generated frame is selected after viewing
the output. Record the video rate and final sprite cadence separately.

Cap this branch at **four clips total and four GPU-hours**, whichever comes
first, including the initial profile. Reserve at least two complete clips for
the selected quality configuration with two fixed seeds. A memory-limited or
underpowered 1.3B result is not a blanket rejection of temporal methods. If no
configuration fits, report a resource limit. If pose, native readability or loop
closure fails, retain that conclusion without optical-flow repair or rerolls.

## 7. Scope, budgets and implementation boundaries

Recommended first approval: **E0 and E1, E2 if visible deformation warrants the
comparison, then E3/E4 as paired cleanup trials if visible cleanup remains.**
Stop when a complete loop is accepted; skip branches that no longer address an
observed problem. Add E5 explicitly if exploring a temporal model is desired
even when the deterministic route is already promising.

| Work | Maximum planned scope |
|---|---|
| Motion and complete part kit | Twelve-frame control; one diagnosed kit revision; up to four corrective drawings |
| Codex generation | Six reusable-part candidates plus four cleanup pilots plus one twelve-frame cleanup batch: **22 requests maximum** |
| Installed ComfyUI image trial | Sixteen pilot images plus one twelve-frame batch: **28 images maximum** |
| Independent Blender comparison | Four stress poses, then twelve frames only if visibly useful; one working day setup cap |
| Optional VACE | Four clips / four GPU-hours; new weights and isolated setup needed |

These are maximum experiment scopes, not promises to consume the budget. Part
authoring is likely the largest human/agent effort: allow roughly one to three
working days for the first complete kit and review, with substantial uncertainty
about missing surfaces. Inference time is measured on the first approved run;
there is no unsupported per-image price or 3090 throughput estimate here.

Use existing `puppet_edit`, `render_layered_puppet` and frame-set processing
entry points. No second editor, engine-side Python/PyTorch dependency, runtime
rig format or new production provider path is part of the experiment.

Keep first-party orchestration under `scripts/` if new tooling is necessary,
and any reusable pure processing under `src/artwork/` only when justified.
Self-contained external Blender/solver spikes may live under `experiments/`,
with no CMake targets, imports from `src/`, or provider calls. Provider requests
are made through the agent's image tool or the approved authoring/tool boundary;
ComfyUI clients must not be added under `experiments/`.

Prefer existing part data and offline corrective overlays before changing
serialized formats. If an engine schema change becomes necessary, it requires
its migration and shipped-definition tests; it is not a shortcut for the pilot.

Retain valuable new inputs and raw inference evidence separately from disposable
renders. Each run records input hashes, source/pose revision, frame order,
generation backend identity when exposed, prompts/revised prompts, model/node
versions, seeds where supported, transform/mask policy, timings, raw outputs,
processed outputs and a result for every screen. A Codex orchestration model
name is not sufficient evidence of which image backend produced an image.

For modified C++: format, run focused tests, lint changed translation units, then
the complete affected test executable and `git diff --check`. For new pure
experiment helpers, add only meaningful tests of phase mapping, registration,
mask preservation or measurement behavior. No broad suite merely to review art.

## 8. Review deliverable and completion

Every surviving branch delivers twelve PNGs, an ordered sheet, a looping
preview with frame-step and ground/part overlays, native and enlarged evidence,
and one comparison table identifying each failure category. Raw and corrected
results remain distinguishable. Whole-character playback is reviewed before
time is spent on subpixel mesh statistics.

The requested experiment outcome is one twelve-frame mouse run accepted for
identity, pose, continuity and game-size appearance, together with a repeatable
way to recreate it. A technically valid import or attractive still does not
complete it.

Stage any game-context comparison in a copied asset root. Production import,
the other five state clips, collider changes and runtime work remain separate
from this experiment approval. After acceptance, plan that integration against
the existing stable Blueprint, six state keys, 32×64 collider and timing
contracts. Move this plan's completed findings into history at closeout.
