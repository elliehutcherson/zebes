"""Reuse two generated material samples on deterministic C++ control shapes.

This is deliberately texture donation, not a fitted or repaired generator
result. One fixed cloth sample and one leather sample shade every pose. Their
coordinates are authored once on the retained near-leg sheet, not per frame.
"""

import math

from PIL import Image, ImageChops, ImageDraw, ImageFilter

from scripts.prepare_sprite_sequence import capsule, leg_image, line


SAMPLES = {"cloth": (1320, 265, 1340, 290), "leather": (1318, 324, 1342, 346)}
SOURCE_SHA256 = "63de72647a6fdba5d9b46535572a271d4ed9e49dc32868b072cca3f166f5bd25"


def samples(source):
    if source.size != (1536, 1024):
        raise ValueError("material samples are calibrated to the retained 1536x1024 near-leg sheet")
    result = {name: source.convert("RGB").crop(box) for name, box in SAMPLES.items()}
    for name, sample in result.items():
        if any(min(pixel) > 220 for pixel in sample.get_flattened_data()):
            raise ValueError(f"{name} sample includes background; revise its explicit atlas rectangle")
    return result


def paint(image, mask, sample, origin, x_axis, y_axis, width, height):
    if min(width, height) <= 0:
        raise ValueError("collapsed material coordinates")
    sx, sy = (sample.width - 1) / width, (sample.height - 1) / height
    transform = (x_axis[0]*sx, x_axis[1]*sx, -(origin[0]*x_axis[0]+origin[1]*x_axis[1])*sx,
                 y_axis[0]*sy, y_axis[1]*sy, -(origin[0]*y_axis[0]+origin[1]*y_axis[1])*sy)
    mapped = sample.transform(image.size, Image.Transform.AFFINE, transform, Image.Resampling.NEAREST,
                              fillcolor=sample.getpixel((sample.width//2, sample.height//2)))
    image.paste(mapped.convert("RGBA"), (0, 0), mask)


def paint_segment(image, sample, a, b, width, mask):
    length = math.dist(a, b)
    if length <= 1e-6:
        raise ValueError("collapsed material segment")
    along = [(b[i]-a[i])/length for i in (0, 1)]
    across = [-along[1], along[0]]
    origin = [a[i] - across[i]*width/2 - along[i]*width/2 for i in (0, 1)]
    paint(image, mask, sample, origin, across, along, width, length+width)


def textured_leg(leg, atlas):
    control = leg_image(leg)
    image = control.copy()
    for a, b, width in ((leg["hip"], leg["knee"], 15), (leg["knee"], leg["cuff"], 13)):
        mask = Image.new("L", image.size)
        capsule(ImageDraw.Draw(mask), a, b, width, 255)
        paint_segment(image, atlas["cloth"], a, b, width, mask)
    boot = Image.new("L", image.size)
    boot_draw = ImageDraw.Draw(boot)
    for key in ("foot", "shaft"):
        boot_draw.polygon([tuple(point) for point in leg[key]], fill=255)
    a = leg["ankle"]
    boot_draw.ellipse((a[0]-4, a[1]-4, a[0]+4, a[1]+4), fill=255)
    paint_segment(image, atlas["leather"], leg["cuff"], a, 13, boot)
    foot_mask = Image.new("L", image.size)
    ImageDraw.Draw(foot_mask).polygon([tuple(point) for point in leg["foot"]], fill=255)
    heel, toe = leg["heel"], leg["toe"]
    length = math.dist(heel, toe)
    along = [(toe[i]-heel[i])/length for i in (0, 1)]
    upper = [-along[1], along[0]]
    if sum(upper[i]*(a[i]-heel[i]) for i in (0, 1)) < 0:
        upper = [-value for value in upper]
    height = max(sum(upper[i]*(point[i]-heel[i]) for i in (0, 1)) for point in leg["foot"])
    paint(image, foot_mask, atlas["leather"], heel, along, upper, length, height)
    rim = ImageChops.subtract(boot, boot.filter(ImageFilter.MinFilter(3)))
    image.paste((57, 32, 23, 255), mask=rim)
    ink = ImageDraw.Draw(image)
    line(ink, [heel, toe], (48, 32, 24, 255), 2)
    line(ink, leg["shaft"][1:3], (173, 108, 56, 255), 2)
    # Geometry is explicitly authoritative in this route. Do not score this
    # constrained alpha as evidence of model pose obedience.
    image.putalpha(control.getchannel("A"))
    return image
