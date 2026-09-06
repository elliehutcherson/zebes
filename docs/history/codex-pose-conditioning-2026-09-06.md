# Codex pose conditioning

Opened 2026-09-05, closed 2026-09-06. Evidence in
`experiments/character_binding/evidence/codex-pose-conditioning/` and the
matching section of `experiments/character_binding/FINDINGS.md`.

## Why it closed

The generator will not obey a skeleton that contradicts what it has learned
running looks like. It draws legs far apart because that is what reads as
running, and a target skeleton asking for legs together does not change that.

Three requests asked for compressed-support, contact, and high-recovery poses.
All three came back as conventional split strides. Figure height varied 17.7%,
the baseline moved 74 px in the wrong phase order, and frames 3 and 10 stayed
0.745 similar despite targeting very different poses. Engineering review
blocked the remaining nine requests.

Measured twice. `matched-pilot-v1` and `matched-pilot-v2-simplified` supplied a
running frame and its skeleton alongside the standing identity, which did not
test the intended contract but hit the same wall.
`standing-skeleton-pilot-v3` corrected the boundary — exactly two images per
request, standing subject then target skeleton — and failed the same way.

## What was settled along the way

- Generation supplies pose and limb separation; it does not supply
  registration. Attempt 2 stated an exact canvas, height, ground row, block size
  and palette. The model honoured none of them, and Codex satisfied the numbers
  afterwards with ImageMagick.
- Registration aligns frames to a shared world ground line, never to each
  figure's bounding box. Normalising bounding boxes deleted the model's real
  flight frame and flattened hip oscillation to zero.
- The approved source art is a 128x128 sprite stored at 256x256, so rig
  coordinates are in doubled space.

## What survived

`rig-bench.json` — 23 points, 22 bones, 12 run poses — is now
`experiments/character_binding/inputs/rig-bench.json` and is the visible
authoring skeleton in the interactive editor. It was drawn in an external
browser tool ("Puppet Rig Bench"); the repository holds the exported snapshot,
not the tool.

The bind pose was traced by hand from the generated `up` frame and owns the
character's proportions. Earlier procedural seeds were deleted for producing
13px shins on a 195px figure.

`src/artwork/skeleton_rig.{h,cc}` keeps the parser and the cycle metrics, which
the editor needs. Its HTML review page and PNG guide renderers were deleted with
the experiment, along with `render_skeleton_rig_review` and three unowned Python
analysis scripts.

The five tracing underlays come from a Codex render whose run cycle is wrong —
the lead foot never alternates. They are useful for limb shape and body height
only, and are not an authoring target.

## What replaced it

Posing the character directly. The interactive editor binds a skeleton and mesh
to approved art, the user moves the joints, and generation is asked only to
clean up the result — never told the character is running.
