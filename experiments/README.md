# Experiments

Start with the [handoff](../docs/handoff.md) and read only the experiment
needed for the task. Plans in `docs/` own requirements; experiment records own
observations, attribution, inputs, hashes, and reproduction.

| Directory | Status and purpose |
|---|---|
| [storybook_mouse](storybook_mouse/README.md) | Blender implementation for the accepted character and subsequent animation studies |
| [mouse_3d](mouse_3d/README.md) | Preserved character, foot-study, and run bundles; also earlier procedural evidence |
| [character_binding](character_binding/README.md) | Superseded 2D/proxy route; retained puppet inputs, tools, and failure evidence |
| [sprite_sequence](sprite_sequence/README.md) | Superseded profile/atlas/painted-part route; final painted run rejected |
| [pose_analogy](pose_analogy/README.md) | Superseded grid/skeleton conditioning proposal; inputs prepared, never submitted |

Keep source assets and versioned evidence at their recorded paths. Snapshot
copies inside bundles are reproduction records; edit the live implementation
under `scripts/` or the experiment root for new work. Keep experiment code
outside the engine build and provider calls under `scripts/`.

Default `rg` searches omit evidence, input media, versioned mouse bundles,
generated `out/` directories, and implementation history. Read linked files
directly or opt into a bounded search:

```bash
rg --no-ignore -n 'support' experiments/mouse_3d/storybook-run-v3/README.md
```
