# Character-binding experiments — superseded

The [accepted storybook 3D mouse](../../docs/mouse-3d-plan.md) replaced this
route on 2026-09-12. No layered-part, boot-generation, or one-arm review gate
here blocks current work.

## Findings worth carrying forward

- Independent generated poses drift in body mass and identity.
- A silhouette or planted contact does not establish limb ownership, boot
  orientation, or convincing motion. Inspect the actual artwork and pose.
- Assign visible pixels explicitly between overlapping parts. Underpaint and
  cast shadows must not enlarge a correct, immutable coat silhouette.
- Validate measurement tools against known controls before judging output.
- Preserve raw results separately from fixed registration and review images.

## Retained material

| Path | Purpose |
|---|---|
| `inputs/` | Source art, traced poses, rigs, and accepted See-through layers |
| `puppet_documents/` | Authored documents consumed by the current puppet tools |
| `puppet_specs/`, `character_specs/` | Legacy spec and Blender-family reproduction inputs |
| `evidence/` | Closed trial receipts, raw results, measurements, and attribution |
| `out/` | Ignored, regenerable render output |
| [HISTORY.md](HISTORY.md) | Earlier implementation and reproduction commands |
| [FINDINGS.md](FINDINGS.md) | Detailed measurements and failed approaches |

The reusable puppet implementation still lives in `src/artwork/`, with
`puppet_edit`, `serve_puppet_editor`, and `render_layered_puppet` under
`scripts/`. Its tests remain under `tests/artwork/`; superseding the art route
does not remove the supported document format or tools.

The final 2D attempts are indexed by the
[sprite-sequence experiment](../sprite_sequence/README.md). The
[boot/leg rejection record](../../docs/history/sprite-boot-control-2026-09-10.md)
and individual `evidence/*/README.md` files retain their specific conclusions.
