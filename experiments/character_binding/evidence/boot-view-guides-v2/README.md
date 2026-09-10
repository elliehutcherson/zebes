# Stouter boot and leg guides

The user approved the v1 view directions for generation and requested thicker,
stouter legs and boots. V2 applies that amendment: thigh width 11→15 working
pixels, calf width 9→13, wider/deeper boot shafts and toe boxes, and a thicker
sole. The reviewed camera, foot directions, heel/toe lengths and contact
registration are unchanged. The cuff center is unchanged while its opening
widens. These proportions are conditioning geometry, not finished artwork.

Source: `../../inputs/boot-view-guide-v2.json`, rendered with
`scripts/prepare_boot_view_guides.py`. The unchanged mouse document hash and
all camera/phase values are in the config and the retained manifest.

The brown `geometry-1024.png` supplies pose and volume to the generator. The
original mouse reference supplies appearance. `mask-1024.png` identifies the
editable region. The colorful `surfaces` images only help a person inspect
which faces are visible; they are not supplied as generation inputs or depth.

Four built-in image-generation requests using these guides are retained in
`../stout-boot-redraw-v1/`. The other guide poses are prepared but not yet
generated. Earlier source guides remain in `../boot-view-guides-v1/`.
