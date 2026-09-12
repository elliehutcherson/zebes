# Roadmap

Updated 2026-09-12.

## Current work

Tech-debt reduction: consolidate retained Python sprite-processing logic into
the existing C++ postprocessor in bounded changes, preserving intended pixels
with regression fixtures. The [handoff](handoff.md) records the next action.

Animation is paused after run 03 was rejected. When the user resumes it, follow
the [3D mouse plan](mouse-3d-plan.md). A convincing run from the accepted model
comes before environment polish, runtime M4, clothing, or production import.

## Parked work

- Character: other clips, production import, and visible runtime transitions
  after the run works; editor import controls in the
  [frame-set pipeline](animation-artwork-pipeline.md#remaining-work).
- Environment: bounded Catacombs silhouette variation and repetition polish.
  See [environment artwork](environment-artwork-plan.md#remaining-bounded-pass).
- Runtime: M4 thread split, bounded I/O and main-thread GPU upload; M5 measured
  host tuning. See [runtime plan](engine-runtime-plan.md).
- Provider integration: final live editor smoke test for
  [Codex image generation](codex-image-generation.md), plus remaining
  [prop lifecycle follow-up](prop-artwork.md).

Architecture documents own subsystem boundaries. Experiment records and
archived roadmaps are evidence, not additional work lists.
