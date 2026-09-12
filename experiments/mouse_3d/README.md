# Mouse 3D asset index

The [3D mouse plan](../../docs/mouse-3d-plan.md) owns current decisions and
remaining work. [storybook_mouse](../storybook_mouse/README.md) owns the live
Blender implementation and tool setup. Animation revisions are paused after
run 03 was rejected as a run.

| Bundle | Verdict |
|---|---|
| [Neutral master](storybook-neutral-v1/README.md) | Accepted character and workflow; preserve unchanged |
| [Run 01](storybook-run-v1/README.md) | Starting point; flat-footed and too slow |
| [Run 02](storybook-run-v2/README.md) | Unsatisfactory; rigid toe-pivot contact did not make the gait convincing |
| [Foot study 01](storybook-foot-study-v1/README.md) | Positively reviewed local foot shape and articulation; source for run 03 |
| [Run 03](storybook-run-v3/README.md) | Rejected as a run; larger visible stride, stronger lean, and rearward push-off needed |

Each bundle retains its source copies, results, checks, hashes, and reproduction
record. The generated review labels predate later user verdicts; the plan owns
the current verdict. Preserve all bundles when making a new revision.

`build_mouse.py`, `forms.py`, and `motion.py` reproduce the earlier procedural
mouse. Their versions and rejected bind/shadow drafts remain evidence, with
details in [HISTORY.md](HISTORY.md). They are not the accepted model's rig or
motion implementation.
