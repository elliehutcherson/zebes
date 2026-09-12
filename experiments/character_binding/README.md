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
| `puppet_specs/`, `character_specs/` | Legacy specs and inputs for the retired Blender-family generator |
| `evidence/` | Closed trial receipts, raw results, measurements, and attribution |
| `out/` | Ignored, regenerable render output |
| [HISTORY.md](HISTORY.md) | Earlier implementation and reproduction commands |
| [FINDINGS.md](FINDINGS.md) | Detailed measurements and failed approaches |

The browser puppet editor and its server/session/workspace code are retired,
as are `render_character_family.py` and `render_mouse_production.py`. Their
source is retained in Git at checkpoint `ede7b18`; historical commands for
these tools require that revision. No authoring server is part of the current
build.

Batch image deformation, silhouette/depth controls, semantic layer import,
`puppet_edit`, and `render_layered_puppet` remain available for offline image
correction and reproduction. Their document formats and headless tests remain
supported. ComfyUI request/upload/workflow tools are also retained; the current
trial runners are image-specific, not a new 3D provider adapter.

The final 2D attempts are indexed by the
[sprite-sequence experiment](../sprite_sequence/README.md). The
[boot/leg rejection record](../../docs/history/sprite-boot-control-2026-09-10.md)
and individual `evidence/*/README.md` files retain their specific conclusions.
