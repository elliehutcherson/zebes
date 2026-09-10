"""Expose complete leg chains and their occlusion in the existing boot guides.

This creates diagnostic images, not artwork or provider requests. It uses the
same boot renderer, camera, anchors and leg widths as the retained v2 guides.
"""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from prepare_boot_view_guides import render_boot
from prepare_run_gap_trial import capsule


COLORS = {"near": (222, 121, 43), "far": (61, 144, 201)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("document", "guides", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("output exists; preserve the previous diagnostic")
    document = json.loads(args.document.read_text())
    manifest = json.loads((args.guides / "manifest.json").read_text())
    config = manifest["camera"]
    if hashlib.sha256(args.document.read_bytes()).hexdigest() != config["document_sha256"]:
        raise ValueError("document no longer matches the retained guide anchors")
    frames = {frame["name"]: frame for frame in document["frames"]}
    guides = {frame["name"]: frame for frame in manifest["frames"]}
    args.output.mkdir(parents=True)
    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 18) if font_path.exists() else ImageFont.load_default()
    small_font = ImageFont.truetype(str(font_path), 15) if font_path.exists() else font
    records = []
    for name in ("reference_10", "reference_04"):
        frame, guide = frames[name], guides[name]
        directory = args.output / name
        directory.mkdir()
        complete, tint, paths = {}, {}, {}
        for side, suffix in (("far", "r"), ("near", "l")):
            anchors = guide["annotations"][side]
            boot, _, _ = render_boot(anchors["heel"], anchors["toe"], config)
            leg = Image.new("RGBA", tuple(config["canvas"]))
            draw = ImageDraw.Draw(leg)
            hip, knee, cuff = frame["pose"]["hip_c"], frame["pose"]["knee_" + suffix], anchors["cuff"]
            capsule(draw, hip, knee, config["proportions"]["thigh_width"], (49, 43, 33, 255))
            capsule(draw, knee, cuff, config["proportions"]["calf_width"], (55, 48, 36, 255))
            complete[side] = Image.alpha_composite(leg, boot)
            complete[side].save(directory / (side + "-complete.png"))
            tint[side] = Image.new("RGBA", leg.size, COLORS[side] + (0,))
            tint[side].putalpha(complete[side].getchannel("A"))
            paths[side] = [hip, knee, cuff, anchors["heel"], anchors["toe"]]
        # The renderer's actual layer sequence, including leg/boot ownership.
        order = [part for part in frame["draw_order"] if part in ("near_leg", "near_boot", "far_leg", "far_boot")]
        owner_order = []
        for part in order:
            owner = part.split("_")[0]
            if owner not in owner_order:
                owner_order.append(owner)
        if owner_order != ["far", "near"]:
            raise ValueError("this diagnostic expects far then near")
        mixed = Image.alpha_composite(tint["far"], tint["near"])
        mixed.save(directory / "leg-identities.png")
        pose_name = "10: folded A in front of standing B" if name.endswith("10") else "4: standing A in front of folded B"
        labels = ["A: near leg, complete", "B: far leg, complete", "A drawn in front of B", "Current flattened guide"]
        panels = [tint["near"], tint["far"], mixed, Image.open(args.guides / name / "geometry.png").convert("RGBA")]
        board = Image.new("RGB", (1120, 365), (245, 243, 237))
        draw = ImageDraw.Draw(board)
        draw.text((16, 10), "Pose " + pose_name + "  |  Diagnostic only; no generation", fill=(25, 28, 32), font=font)
        for index, (label, panel) in enumerate(zip(labels, panels, strict=True)):
            cell = Image.new("RGBA", (256, 256), (255, 255, 255, 255))
            cell = Image.alpha_composite(cell, panel)
            mark = ImageDraw.Draw(cell)
            if index < 3:
                sides = ("near",) if index == 0 else ("far",) if index == 1 else ("far", "near")
                for side in sides:
                    hip, knee, cuff, heel, toe = paths[side]
                    mark.line([tuple(point) for point in (hip, knee, cuff)], fill=(25, 28, 32, 255), width=1)
                    mark.line([tuple(heel), tuple(toe)], fill=(25, 28, 32, 255), width=1)
                    for x, y in (hip, knee, cuff):
                        mark.ellipse((x - 1.5, y - 1.5, x + 1.5, y + 1.5), fill=(255, 255, 255, 255))
            zoom = cell.crop((85, 138, 180, 230)).resize((266, 258), Image.Resampling.NEAREST)
            board.paste(zoom.convert("RGB"), (index * 280 + 7, 56))
            draw.text((index * 280 + 12, 35), label, fill=(25, 28, 32), font=font)
        draw.text((16, 328), "White dots: hip, knee, boot opening. Colored areas include hidden portions normally covered by the coat.", fill=(25, 28, 32), font=small_font)
        board.save(directory / "ownership-review.png")
        records.append({"name": name, "draw_order": order, "paths": paths,
                        "near": "A", "far": "B", "status": "diagnostic; generation inputs still to be selected"})
    (args.output / "manifest.json").write_text(json.dumps({"source_guide_config": config, "frames": records,
                                                          "provider_calls": 0}, indent=2) + "\n")
    print("Prepared two ownership diagnostics and complete per-leg geometry layers; no generation.")


if __name__ == "__main__":
    main()
