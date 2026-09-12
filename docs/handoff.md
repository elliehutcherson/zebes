# Active handoff

Updated 2026-09-12. **Next: consolidate retained sprite-processing logic.**
Obsolete authoring servers and procedural character generators are retired,
and the storybook Blender tools share `rig_support.py`.
Animation remains paused after run 03 was rejected.

## Next implementation

Identify a bounded piece of useful Python sprite processing to consolidate
under the [existing C++ postprocessor](../src/artwork/generated_artwork_postprocessor.cc).
Start with the [asset-tool index](../scripts/README.md#prepare-generated-artwork-for-import)
and check callers before choosing the scope. Python and C++ matte/resize
policies differ: capture intended pixels in regression fixtures before
changing implementations.

Prefer C++ for reusable processing, with Python at Blender/model-library
boundaries and for thin orchestration; see the [style guide](style-guide.md).
Retired generators are available at `ede7b18` and in frozen source snapshots;
do not port them. The experiment indexes own the retirement and reproduction
notes. The [storybook README](../experiments/storybook_mouse/README.md#verification)
owns current animation-tool verification commands.

## Preserve and consult

Keep the accepted neutral master, foot study, all three run bundles, and
reference evidence unchanged. The [3D plan](mouse-3d-plan.md) owns decisions;
bundle records own hashes and reproduction. The [experiment index](../experiments/README.md)
and [asset tools](../scripts/README.md) identify retained capabilities.

ComfyUI integration, useful 2D image manipulation, Sprite playback, frame-set
import, and prop/parallax processing remain in scope for maintenance.
Keep the current asset-storage layout.
Read only relevant [architecture](architecture.md) sections and use bounded
`rg --no-ignore` searches for historical evidence.
