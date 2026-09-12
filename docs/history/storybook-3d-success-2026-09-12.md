# Accepted success: storybook mouse and 3D-to-sprite workflow

On 2026-09-12, after inspecting the actual neutral 3D model and its turntable,
the user said:

> You did it. This is clearly the way. This is fantastic. Please document this
> as a success. Make this the new plan of record.

They also requested that the model be saved in the repository and asked to try
a running animation from it. This is explicit acceptance of the neutral
character and the 3D authoring route. It supersedes the earlier unresolved
neutral-art status and makes running animation the next milestone.

The [acceptance record](../../experiments/mouse_3d/storybook-neutral-v1/acceptance.json)
pins the exact accepted Blender file, textured GLB, packed texture and sprite
previews with SHA-256 hashes. The Blender file contains `Mouse_Neutral` and
the 20-bone `Mouse_Study_Rig`, with no linked model libraries. Its texture is
packed; the GLB also embeds the texture. These files are usable without the
remote Hunyuan weights/cache or temporary reconstruction directory.

The successful route was a clean reference image, one Hunyuan3D-2 shape sample,
explicit mesh cleanup, Blender materials and body binding, and offline sprite
export. Raw inputs, raw/clean geometry, failed diagnostics, exact source and
settings remain in the [asset record](../../experiments/mouse_3d/storybook-neutral-v1/README.md).
Ten focused checks validate the implementation; the user's verdict above
establishes the artistic success.

The accepted master is preserved unchanged. The temporary rig and known hand,
tuft and facial limitations can be refined when needed; they do not invalidate
the acceptance or block an initial run attempt. The existing three poses are
deformation checks, so a complete running cycle still needs to be authored and
reviewed.

The [new plan of record](../mouse-3d-plan.md) directs the next conversation to
load this saved model, make a separate run working copy, author grounded
locomotion and export a consistent sprite sequence. It explicitly avoids
regenerating the accepted character or returning to per-frame generated limbs.
