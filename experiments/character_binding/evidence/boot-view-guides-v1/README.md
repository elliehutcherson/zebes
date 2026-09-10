# Proposed boot-view guides

Prepared 2026-09-09 for visual review. **No generation submitted with these
inputs.** The material colors and box geometry are placeholders for a later
whole-leg/boot redraw, not proposed finished sprites.

The source of truth is `../../inputs/boot-view-guide-v1.json` and
`scripts/prepare_boot_view_guides.py`. The config freezes the accepted mouse
document's hash and records source sole anchors, one shared camera (35° yaw,
8° elevation), proposed sole angles and support phases. Every frame's actual
heel, toe and cuff positions are retained in `manifest.json`.

The boot consists of three simple boxes for the sole, toe box and shaft. The
script uses orthographic projection and a triangle z buffer to determine which
surfaces are visible. A separate calf placeholder joins the existing knee to
the projected boot opening. The current source body, coat, arms and head remain
outside the proposed editing region. The user still needs to review the camera,
foot directions, boot proportions and leg shapes.

Heel-to-toe sole direction is separate from the rig's ankle-to-toe ray. The
source's rigid boot transform supplies heel travel and sole length; explicit
phase directions replace the inherited boot angle. Support heel/toe endpoints
are placed on the ground at y=214. Other boots only move upward to avoid
penetration, with six pixels of flight clearance. This is a boot-specific
contact adjustment, not whole-sprite bounding-box normalization. The current
mouse skeleton document is unchanged; guide cuff/sole targets are candidate
replacement landmarks, not already integrated skeletal controls.

Near and far boots use the same camera. Pose 1 exposes the near sole and hides
the far sole; later phases change the visible surfaces. The initial draft
inherited an upward-tilted supporting boot in pose 10 and was superseded.
Intermediate drafts remain under ignored `out/047…049` directories.

Per-frame files:

- `original.png`: unchanged old puppet on white.
- `geometry.png` / `geometry-1024.png`: rough material-colored lower-body guide.
- `surfaces.png` / `surfaces-1024.png`: face labels (purple sole, green cuff,
  blue sides, orange toe, yellow legs).
- `mask.png` / `mask-1024.png`: proposed editable region including complete
  old/new legs and boots, while protecting unrelated artwork.

Face-label colors are **not depth**. No full-character physical depth map is
exported or proposed as a model input. The colored face view is for inspection.
The intended initial redraw would use the material guide, identity reference
and reviewed mask.

The combined review is `../run-gap-control-v1/review.html`; its "New boot guides"
view plays all twelve frames and exposes the proposed mask and landmark overlay.

```bash
build/tileset-venv/bin/python scripts/prepare_boot_view_guides.py \
  --document experiments/character_binding/puppet_documents/mouse_run_reference_v2.json \
  --config experiments/character_binding/inputs/boot-view-guide-v1.json \
  --render experiments/character_binding/out/046-run-gap-inputs/baseline \
  --output experiments/character_binding/out/boot-guide-reproduction
```

Preparation refuses an existing output directory. Reproduce the baseline with
`render_layered_puppet` as documented in `../run-gap-inputs-v1/README.md` if needed.
