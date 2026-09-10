# Four-pose stout leg and boot redraw

Four built-in `image_gen` requests completed on 2026-09-09 for poses 1, 7, 10
and 12. The user approved the view guides as generation inputs with thicker
legs and boots. That specific amendment is implemented in `boot-view-guides-v2`.
No additional confirmation was needed for the requested proportion adjustment.

Open `review.html` to compare the stouter geometry, raw model redraw and two
compositing policies. This is a four-pose pilot, not a complete animation.

## Inputs and execution

Every request uses the same three roles: material-colored posed guide; fixed
original character appearance reference; and white-edit/black-preserve region
guide. The colorful surface-ID image was not sent. The region guide is an
ordinary reference image, not an enforced provider inpainting mask. The tool
does not expose a model identifier or reproducible seed.

`manifest.json` and each frame's `prompt.txt` retain the exact prompts, input
hashes, provider source path and output hash. `raw-output.png` is untouched.
All four outputs arrived as **1254×1254**, despite requesting 1024×1024, with
opaque alpha. `canvas-mapped.png` applies only a uniform whole-canvas resize to
256×256. No per-character fitting, cropping or translation hides pose changes.
RGBA and 48×48 previews use the existing source-aware white-matte extraction.

## Finding

The model replaces the box proxies with rounded leather boots, cuffs, fabric
folds and stout shaded legs. Both contact poses retain a forward-facing sole,
pose 10 retains a high tucked boot and a low supporting boot, and pose 12
remains airborne. These are qualitative observations, not full-cycle approval
or evidence of exact joint matching.

For a simple registration diagnostic, the lowest foreground row at 256px is
213/214/215/197 in the mapped outputs, versus guide rows 214/214/214/208 for
poses 1/7/10/12 respectively. Thus the airborne result's lowest boot is about
11 working pixels higher. The method follows broad pose structure while still
changing local geometry and spacing. The raw output also redraws the upper
character, which is why the separate composites retain original artwork there.

## Compositing comparison

`composite.png` uses the originally declared repair mask. All black-region
pixels remain exactly the original. Gray edge pixels permit proportional
blending; a regression check covers this distinction. The original narrow
mask clips newly drawn boot pixels, most visibly the tucked boot in pose 10.

`wide-composite.png` is a **post-hoc compositing comparison**, not a new model
request or a mask that was sent to the provider. It uses one fixed rule for
all four poses: x=40…220, y=hip_y−8…230, excluding the original rendered body,
head, arms and tail pixels. No generated bounds or adaptive tracing define
that region. It recovers the cut-off boot while retaining those protected
part pixels. New pixels beyond the old protected silhouettes can still appear,
including minor coat-outline changes, so this policy needs visual evaluation.
The original raw outputs and strict composites remain intact.

`wide-mask.png`, `wide-composite-rgba.png`, `wide-native-48.png` and the manifest's
`wider_comparison` fields retain that distinction. This is evidence for a more
appropriate whole-leg compositing region, not proof of perfect frame alignment.

## Reproduction and checks

`scripts/boot_redraw_pilot.py` prepares inputs, records received outputs, and
builds the two composites without provider calls. `scripts/render_stout_boot_review.py`
builds the review from retained files. The final prompt set lives in the four
frame directories. Use `--help` for explicit paths. Scripts refuse overwriting
completed results and record a received provider output before postprocessing,
so a processing failure cannot silently erase its request count.

Five focused tests pass: boot surface visibility, projected sole pins, first
pose visibility, stout volume with unchanged pins, and feathered-mask handling.
All four strict and broader composites were also checked against the actual
input pixels outside their respective masks. Browser review checks pose/mask
selection and native-size display. No production import was performed.
