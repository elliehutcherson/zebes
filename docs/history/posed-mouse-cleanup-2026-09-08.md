# Posed-mouse cleanup experiment

Run completed 2026-09-08; the complete loop is available for art review.
This is experiment evidence, not production acceptance or a new gate.

The user accepted the corrected twelve-pose mouse as a starting point, while
noting imperfect foot directions. Those poses stayed unchanged. The question
was whether generation could repair an already posed character more faithfully
than the earlier skeleton-reference requests.

## Requests and inputs

Sixteen built-in `image_gen` calls: a four-frame pilot, followed by twelve fresh
frames. No CLI/API fallback, training, ComfyUI job or production import ran.
The image backend's model identifier and seed were not exposed by the tool.

Every request used three ordered images:

1. the C++ posed mouse, enlarged from 256 to 1024 by nearest-neighbor sampling;
2. the same original running mouse as an appearance reference; and
3. a binary repair-region guide, supplied as an ordinary reference image.

The third image is not a dedicated inpainting channel. Repair regions cover
arms, legs and missing trouser connections. The head, boots, tail, scarf and
belt have protected envelopes. The request asks for local sleeve/trouser repairs
while keeping pose, placement and identifying details unchanged. Prompts and
input hashes are retained in each manifest and `prompt.txt`.

The pilot used poses 1, 7, 10 and 12. All four results broadly retained their
different arrangements, including the folded leg in pose 10. They nevertheless
repainted protected details and returned opaque checkerboards instead of
transparency. All were 1254×1254 despite the requested 1024×1024 canvas.

The full-cycle trial changed the backdrop contract to an explicit plain white
matte in the posed input, identity input and output prompt. It used all twelve
poses, in order, with one fresh result each. There were no selective rerolls
and no pilot frames were spliced into the cycle. The full batch tests motion
despite imperfect local repairs, consistent with the user's suspension of the
old prerequisite gates. It consumes the planned total of sixteen frame edits.

## What the comparisons contain

Raw provider outputs are untouched. All processing uses whole-canvas scaling;
there is no per-character crop, translation, bounding-box normalization,
optical interpolation or phase reordering.

The pilot's extracted previews remove the observed light achromatic background
with a documented color predicate. This is a diagnostic workaround, and small
tinted background residue remains in the strict-tolerance pose-12 extraction;
its extracted bounds must not be mistaken for anatomy measurements.

The full batch removes the connected near-white exterior. It also removes
enclosed white regions already marked transparent in the source, while keeping
interior white highlights over existing opaque source pixels. This derived
alpha is preprocessing, not native provider transparency.

Two complete output sequences are retained:

- **Whole redraw:** the model's complete image after matte extraction.
- **Only permitted repairs:** the extracted result inside the fixed mask,
  with the original posed pixels restored everywhere else. All twelve frames
  report zero changed pixels outside their masks.

The browser compares these with the original puppet at 48px and with 128px
detail previews. The RGBA sheets are six columns by two rows of 48px frames.
The APNG previews have twelve frames and a total duration of 1000 ms; decoded
pixels were checked against the individual frames without palette quantization.

## Observations and remaining work

The already posed artwork supplied a substantially more useful pose signal
than the archival skeleton-only attempts: the output retained the compressed,
crossed and airborne arrangements instead of reducing them all to generic
split strides. This is an engineering observation, not an isolated causal test
of skeleton calibration alone; the request mechanism also changed.

The plain white matte removed the checkerboard failure. Whole redraws close
more garment and leg gaps, but do not preserve exact facial/costume pixels.
Protected-pixel compositing keeps identity pixels exact but exposes mismatches
where newly painted parts meet the old artwork. It cannot guarantee a usable
connection merely by copying pixels inside a mask.

Frames 5, 10 and 11 still need attention around the legs and boots. Pose 10's
supporting boot gap survives in the raw redraw. Foot directions remain a known
input limitation. These defects are visible in the complete retained batch;
they were not selectively regenerated or removed from playback.

Next, review the full loop at native size. Keep the current pose set while
repairing the incomplete leg artwork and foot attachment unless motion review
points to a specific pose problem. A subsequent ComfyUI comparison can test a
real sampler mask against these soft reference-image masks. Its exact inputs
must be shown before generation, as the user requested.

## Evidence

- [Pilot manifest](../../experiments/character_binding/evidence/pose-cleanup-pilot-v1/manifest.json)
- [Twelve-frame manifest](../../experiments/character_binding/evidence/pose-cleanup-cycle-v2/manifest.json)
- [Whole-redraw sheet](../../experiments/character_binding/evidence/pose-cleanup-cycle-v2/model-sheet.png)
- [Protected-pixel sheet](../../experiments/character_binding/evidence/pose-cleanup-cycle-v2/composite-sheet.png)
- [Browser comparison](../../experiments/character_binding/evidence/pose-cleanup-cycle-v2/review.html)

`scripts/pose_cleanup_pilot.py` prepares and processes inputs without making
provider calls. `scripts/render_cleanup_cycle_review.py` builds the complete
review. Three focused tests cover background extraction and protection against
overwriting already-submitted inputs. Patch whitespace checks pass.
