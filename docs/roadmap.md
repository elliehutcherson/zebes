# Roadmap

Updated 2026-09-08.

## Current priority: a working twelve-frame mouse run

The project is stalled on animation. Finish the green-coated mouse's run before
resuming environment polish, runtime threading or the old track/gate sequence.

The [approved experiment plan](sprite-run-experiment-plan.md) is the work list;
[handoff.md](handoff.md) is the current state. First repair and review the source
skeleton, limb mapping and twelve poses. The user must see and approve the
actual artwork/pose/conditioning inputs before generation. Corrected retries of
earlier methods are allowed when they answer a remaining problem.

Success is a run the user can see working, with consistent identity and smooth
motion. Old art gates, one-arm prerequisite chains and numerical acceptance
thresholds are suspended. Diagnostics remain useful for explaining defects;
software validation and asset integrity remain engineering requirements.

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
