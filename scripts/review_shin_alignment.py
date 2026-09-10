"""Compare the old cuff kink with a continuous knee-to-ankle guide."""

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from prepare_boot_view_guides import render_boot
from prepare_run_gap_trial import capsule


def part(config, anchors, knee, hip):
    boot, _, measured = render_boot(anchors["heel"], anchors["toe"], config, knee)
    leg = Image.new("RGBA", (256, 256))
    draw = ImageDraw.Draw(leg)
    capsule(draw, hip, knee, config["proportions"]["thigh_width"], (49, 43, 33, 255))
    capsule(draw, knee, measured["cuff"], config["proportions"]["calf_width"], (55, 48, 36, 255))
    return Image.alpha_composite(leg, boot), measured


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("document", "old-guides", "new-guides", "generated", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("output exists; preserve earlier comparisons")
    old = json.loads((args.old_guides / "manifest.json").read_text())
    new = json.loads((args.new_guides / "manifest.json").read_text())
    document = json.loads(args.document.read_text())
    records = []
    selected = None
    args.output.mkdir(parents=True)
    for frame, old_frame, new_frame in zip(document["frames"], old["frames"], new["frames"], strict=True):
        if frame["name"] != old_frame["name"] or frame["name"] != new_frame["name"]:
            raise ValueError("pose order differs")
        for side, suffix in (("near", "l"), ("far", "r")):
            knee, hip = frame["pose"]["knee_" + suffix], frame["pose"]["hip_c"]
            old_part, old_points = part(old["camera"], old_frame["annotations"][side], knee, hip)
            new_part, new_points = part(new["camera"], new_frame["annotations"][side], knee, hip)
            records.append({"frame": frame["name"], "side": side, "before": old_points, "after": new_points})
            if frame["name"] == "reference_10" and side == "near":
                old_part.save(args.output / "old-part.png")
                new_part.save(args.output / "corrected-part.png")
                white_part = Image.alpha_composite(Image.new("RGBA", new_part.size, "white"), new_part)
                white_part.crop((88, 136, 184, 232)).resize((1024, 1024), Image.Resampling.NEAREST).save(args.output / "corrected-input-1024.png")
                selected = [(old_part, old_points), (new_part, new_points)]
    generated = Image.open(args.generated).convert("RGBA")
    selected.append((generated, None))
    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 19) if font_path.exists() else ImageFont.load_default()
    small = ImageFont.truetype(str(font_path), 16) if font_path.exists() else font
    board = Image.new("RGB", (1020, 432), (245, 243, 237))
    draw = ImageDraw.Draw(board)
    titles = ["Old guide: kink at cuff", "Corrected guide: straight lower leg", "Rejected angle; retain finish reference"]
    for index, ((image, points), title) in enumerate(zip(selected, titles, strict=True)):
        canvas = Image.alpha_composite(Image.new("RGBA", image.size, "white"), image)
        if points:
            line = ImageDraw.Draw(canvas)
            line.line([tuple(points[key]) for key in ("knee", "cuff", "ankle")], fill=(24, 125, 197, 255), width=1)
            for key in ("knee", "cuff", "ankle"):
                x, y = points[key]
                line.ellipse((x-1.6, y-1.6, x+1.6, y+1.6), fill="white", outline=(24, 125, 197, 255), width=1)
        crop = canvas.crop((88, 136, 184, 232)).resize((324, 324), Image.Resampling.NEAREST)
        board.paste(crop.convert("RGB"), (index * 340 + 8, 44))
        draw.text((index * 340 + 8, 12), title, font=font, fill=(25, 28, 32))
        if points:
            draw.text((index * 340 + 8, 379), f"Calf-to-shaft change: {points['calf_shaft_angle_degrees']:.1f} degrees", font=small, fill=(25, 28, 32))
    draw.text((8, 408), "Guide markers: knee, cuff, proxy ankle. The knee stays bent; no extra joint at the boot opening.", font=small, fill=(25, 28, 32))
    board.save(args.output / "comparison.png")
    (args.output / "measurements.json").write_text(json.dumps({"status": "guide correction only; no generation", "measurements": records}, indent=2) + "\n")
    print("Retained 24 before/after axis measurements and the pose-10 guide comparison.")


if __name__ == "__main__":
    main()
