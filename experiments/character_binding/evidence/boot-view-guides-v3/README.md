# Calf-aligned boot shafts

The user chose independent rendering and rejected the kink in the generated
folded leg. V3 corrects a contradiction in the geometry guide: a boot shaft
should continue the calf's knee-to-ankle line while the foot has its own angle.
This is a draft input correction for review; no generation used these files.

The source config is `../../inputs/boot-view-guide-v3.json`; the renderer is
`scripts/prepare_boot_view_guides.py`. Legacy configs still use the old
foot-aligned shaft and retain their old images. `shaft_alignment: shin` requires
an explicit knee, computes a shaft direction through that knee and the boot's
proxy ankle, and refuses a shaft that would extend past the knee.

Camera, knee positions, heel/toe pins, foot pitch, stout proportions and support
registration remain from v2. The cuff position and boot shaft orientation now
follow the lower leg. Every frame's metadata includes the proxy ankle, cuff,
knee and measured calf-to-shaft angle. These landmarks are candidates for the
independent-layer binding workflow; the original puppet document is unchanged.

The material and surface-ID renders, 1024px versions and full-leg masks follow
the previous guide layout. Surface labels are not physical depth. The isolated
pose-10 comparison and full angle audit are in `../shin-alignment-review-v1/`.
