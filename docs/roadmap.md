# Roadmap

Updated 2026-09-12.

## Current priority: run animation from the accepted 3D mouse

The user accepted the neutral storybook mouse and the 3D-to-sprite workflow as
a success. Animate this saved model before resuming environment polish,
runtime threading or the older track/gate sequence.

The [3D mouse plan of record](mouse-3d-plan.md) is the work list;
[handoff.md](handoff.md) identifies the accepted master and the next-conversation
handoff. Load the saved Blender model, make a run working copy, author grounded
locomotion and export consistent sprite frames. Do not regenerate the accepted
character or resume the superseded per-frame/layered-part generation route.

The next success is a run the user can see working, with stable identity,
readable limb motion and convincing ground contact. Rig/weight fixes should
address actual motion defects. Hand, tuft, facial and garment polish are not
prerequisites. Software validation and protection of the accepted model remain
engineering requirements.

## Parked work

- Environment: finite Catacombs silhouette variation and repetition polish.
  See [environment artwork](environment-artwork-plan.md).
- Runtime: M4 thread split, bounded I/O and main-thread GPU upload; M5 host
  tuning. See [runtime design](engine-runtime-plan.md).
- Integration: other character clips, production import, live transitions and
  editor import controls after the run works.
- Editor debt: full polygon clipping, source-rig update semantics and incidental
  UI polish. Fix only what directly obstructs current animation authoring.
- General cleanup: retain the earlier non-blocking items in the
  [previous handoff](history/handoff-before-run-calibration-2026-09-08.md).

The [previous roadmap](history/roadmap-before-run-calibration-2026-09-08.md)
preserves prior sequencing and completed tracks. Its gates are historical,
not a second active work list.
