# Active handoff

Updated 2026-09-12. **The repository cleanup pass is complete; animation
remains paused.** Run 03 was rejected as a running animation.

## Read next

- [Roadmap](roadmap.md): remaining work and sequencing.
- [3D mouse plan](mouse-3d-plan.md): accepted source, current verdict, and the
  contract for the next animation revision.
- [Experiment index](../experiments/README.md): retained evidence and live tools.
- [Architecture](architecture.md): read only the relevant domain section.

Verification: 54 focused tests, CMake configuration, documentation links, and
`git diff --check` passed. All six storybook bundles (1,220 files) matched their
pre-cleanup hashes. No engine behavior changed.

The accepted neutral master, positively reviewed foot study, and all three run
bundles must remain unchanged. Their READMEs own reproduction and measurements;
the plan owns decisions. Do not copy their chronology or hashes into this file.

## Context discipline

Keep this handoff to entry points, current blockers, and the next action.
Completed milestones belong in history or version control. Default searches
exclude archived docs and experiment evidence; use a linked path or a bounded
`rg --no-ignore` search when those records are needed.

Continue animation when the user resumes that workstream; the
[plan](mouse-3d-plan.md#next-run) records the deferred direction.
