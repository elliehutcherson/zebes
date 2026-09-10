"""Prepare/retain the three-image leg-ownership test. No provider calls.

The isolated legs share one predeclared source crop and invert that transform
after generation. Their ordering is owned by the compositor, not inferred from
the combined generated picture. New output revisions never replace old ones.
"""

import argparse
import hashlib
import json
import shutil
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

if __package__:
    from .pose_cleanup_pilot import extract_white_matte
else:
    from pose_cleanup_pilot import extract_white_matte


CROP = (88, 136, 184, 232)
COLORS = {"near": (222, 121, 43), "far": (61, 144, 201)}
COMMON = """Use case: precise-object-edit
Render finished pixel-art clothing matching the original mouse reference: thick dark brown trousers and stout rounded brown leather boots with cuffs, dark soles, restrained fabric folds and leather shading. Match the original pixel clusters, palette, texture and outlines. Keep the substantial proportions of the geometry. Round crude cuboid corners rather than simply texturing boxes.
Return one image on the same plain white 1024 x 1024 canvas. Preserve the guide's placement and scale; do not center, enlarge, crop or rearrange it. No text, labels, arrows, colored guide lines, checkerboard, ground shadow or scenery in the output.
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def on_white(image):
    return Image.alpha_composite(Image.new("RGBA", image.size, "white"), image.convert("RGBA")).convert("RGB")


def prepare(args):
    if args.output.exists():
        raise ValueError("output exists; choose a new trial revision")
    if args.frame == "reference_04" and not args.combined_only:
        raise ValueError("pose 4 follow-up currently supports the combined redraw only")
    ownership = json.loads((args.ownership / "manifest.json").read_text())
    pose = next(frame for frame in ownership["frames"] if frame["name"] == args.frame)
    source = args.ownership / args.frame
    guides = args.guides / args.frame
    args.output.mkdir(parents=True)
    shutil.copyfile(args.identity, args.output / "identity.png")
    shutil.copyfile(args.render / "working" / (args.frame + ".png"), args.output / "source-rgba.png")
    for name in ("body", "head", "tail", "far_arm", "near_arm"):
        shutil.copyfile(args.render / "part-poses" / name / (args.frame + ".png"), args.output / (name + ".png"))
    originals = {side: Image.open(source / (side + "-complete.png")).convert("RGBA") for side in ("near", "far")}
    entries = []
    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 24) if font_path.exists() else ImageFont.load_default()
    for name in (("combined",) if args.combined_only else ("combined", "near", "far")):
        directory = args.output / name
        directory.mkdir()
        if name == "combined":
            shutil.copyfile(guides / "geometry-1024.png", directory / "edit-input.png")
            # A full-color leg-identity cutaway, separate from the material image.
            ids = Image.open(source / "leg-identities.png").convert("RGBA")
            diagram = on_white(ids).crop(CROP).resize((1024, 1024), Image.Resampling.NEAREST)
            draw = ImageDraw.Draw(diagram)
            for side, label in (("far", "B"), ("near", "A")):
                points = [[(x - CROP[0]) * 1024 / 96, (y - CROP[1]) * 1024 / 96] for x, y in pose["paths"][side]]
                draw.line([tuple(p) for p in points[:3]], fill=(30, 30, 30), width=5)
                for index, point in enumerate(points[:3]):
                    x, y = point
                    draw.ellipse((x-8, y-8, x+8, y+8), fill="white", outline="black", width=2)
                    if index == 1:
                        draw.text((x + 12, y - 28 if side == "near" else y + 12), label + " knee", fill="black", font=font)
            a_role, b_role = ("folded", "standing") if args.frame == "reference_10" else ("standing", "folded")
            draw.text((22, 26), f"A / ORANGE: {a_role} leg in front", fill=(80, 43, 20), font=font)
            draw.text((22, 59), f"B / BLUE: {b_role} leg behind", fill=(25, 70, 100), font=font)
            diagram.save(directory / "ownership-guide.png")
            # Wider region is declared before this trial, based on body anatomy.
            mask = Image.new("L", (256, 256))
            ImageDraw.Draw(mask).rectangle((40, int(pose["paths"]["near"][0][1] - 8), 220, 230), fill=255)
            for part in ("body", "head", "tail", "far_arm", "near_arm"):
                alpha = Image.open(args.output / (part + ".png")).getchannel("A").point(lambda v: 255 if v else 0)
                mask = ImageChops.subtract(mask, alpha)
            mask.save(directory / "mask.png")
            mask.resize((1024, 1024), Image.Resampling.NEAREST).save(directory / "region-guide.png")
            prompt = COMMON + """
Image 1 is the exact whole-mouse EDIT TARGET with rough lower-body geometry. Image 2 supplies character APPEARANCE ONLY. Image 3 is a magnified leg-identity cutaway: ORANGE A and BLUE B are labels, not costume colors. Image 4 is the editable-region guide: white allows redraw, black means preserve.
Redraw only the legs and boots, keeping the original upper character, coat, scarf, belt, hands, ears, face and tail unchanged. Crucially preserve WHICH KNEE CONNECTS TO WHICH BOOT:
A is the NEAR folded leg. Its thigh travels down-right to the forward knee, then its calf bends BACK LEFT to the HIGH boot. This folded knee/calf lies IN FRONT of the standing leg.
B is the FAR standing leg, behind A. B continues almost vertically down to the LOW planted boot on screen-right. Do not connect A's projecting knee to B's low boot. Preserve a readable overlap edge where A's horizontal folded calf crosses B's vertical leg. Draw both legs in the original trouser colors, with natural shading, not orange/blue.
The high boot belongs to A and the low boot belongs to B. Keep the high/low positions and the crossed topology; do not substitute a generic running stride. The diagnostic cutaway includes portions hidden by the coat, which should remain hidden in this full-character output.
"""
            refs = ["edit-input.png", "../identity.png", "ownership-guide.png", "region-guide.png"]
            if args.frame == "reference_04":
                prompt = COMMON + """
Image 1 is the exact whole-mouse EDIT TARGET with rough lower-body geometry. Image 2 supplies character APPEARANCE ONLY. Image 3 is a magnified leg-identity cutaway: ORANGE A and BLUE B are labels, not costume colors. Image 4 is the editable-region guide: white allows redraw, black means preserve.
Redraw only the legs and boots, preserving the entire original upper character and coat. Crucially keep the connections shown in the cutaway:
A is the NEAR STANDING LEG IN FRONT. It continues nearly vertically downward to the LOW planted boot. Its continuous shin must remain visible in front of the crossing folded calf.
B is the FAR FOLDED LEG BEHIND A. B's thigh goes down-right to its forward-projecting knee, then B's calf doubles BACK LEFT behind A's vertical leg and ends in the HIGH boot. The projecting folded knee belongs to B's high boot, never to A's low boot.
This is the opposite overlap from a foreground-folded-leg pose. Preserve the cutaway's visible/hidden boundaries, with the orange standing leg covering the blue folded calf at the crossing. Final trousers use the original dark brown colors and natural overlap shading, not orange or blue. Keep both boot positions and the existing camera, canvas and character placement.
"""
        else:
            guide = originals[name].crop(CROP)
            guide.save(directory / "source-part.png")
            on_white(guide).resize((1024, 1024), Image.Resampling.NEAREST).save(directory / "edit-input.png")
            prompt = COMMON + ("""
Image 1 is the EDIT TARGET: exactly ONE COMPLETE FOLDED TROUSER LEG AND BOOT, isolated on white. Image 2 is an appearance reference for materials and style only. Do not draw that character or copy its full-body pose.
Keep this ONE leg folded in the exact triangular arrangement of Image 1. Starting at the hip attachment at the top, the thigh goes DOWN-RIGHT to its projecting knee. The calf doubles BACK LEFT from that knee into the high boot. The knee, folded calf and high boot all belong to this one leg. Preserve the negative space inside the bend where present. Do not unfold or straighten the leg. Draw its complete hidden surfaces too, because a separate coat layer will cover them later.
""" if name == "near" else """
Image 1 is the EDIT TARGET: exactly ONE COMPLETE STANDING TROUSER LEG AND BOOT, isolated on white. Image 2 is an appearance reference for materials and style only. Do not draw that character or copy its full-body pose.
Keep this ONE leg nearly vertical in the exact pose of Image 1: hip attachment at top, subtle knee bend, calf continuing down into the low boot with its sole flat and toe pointing right. Do not add a second projecting knee or a folded leg. Draw its complete hidden surfaces too, because another leg and a separate coat layer will overlap it later.
""") + """
Output only the single connected trouser-leg/boot asset. Exactly one hip attachment, one knee, one calf and one boot. No torso, face, mouse body, arms, tail, skirt, cape, second leg or second boot. Preserve the full white canvas and the existing asset position, size and boot direction. Finish the exposed fabric at the top as an attachment area, not a waist or second limb.
"""
            refs = ["edit-input.png", "../identity.png"]
        (directory / "prompt.txt").write_text(prompt)
        entries.append({"name": name, "status": "prepared", "prompt": prompt, "reference_paths": refs,
                        "input_sha256": digest(directory / "edit-input.png"), "crop": None if name == "combined" else list(CROP)})
    manifest = {"experiment": args.frame + " combined leg IDs" + (" opposite-crossing follow-up" if args.combined_only else " versus independently completed legs"),
                "authorization": "user approved the three-image comparison and planned opposite-crossing follow-up", "provider_calls": 0,
                "model_id": None, "identity_sha256": digest(args.identity), "source_ownership": ownership,
                "isolated_crop": list(CROP), "registration": "fixed crop before generation, inverse whole-canvas scaling only",
                "composite_order": ["far_arm", "far", "near", "tail", "body", "near_arm", "head"], "entries": entries}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared {len(entries)} exact target(s) with fixed registration; no generation.")


def receive(args):
    manifest_path = args.output / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    entry = next(entry for entry in manifest["entries"] if entry["name"] == args.name)
    directory = args.output / args.name
    if entry["status"] not in ("prepared", "received"):
        raise ValueError("result already retained")
    if entry["status"] == "prepared":
        shutil.copyfile(args.raw, directory / "raw-output.png")
        entry.update(status="received", raw_sha256=digest(args.raw), raw_source=str(args.raw))
        manifest["provider_calls"] += 1
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    elif digest(args.raw) != entry["raw_sha256"]:
        raise ValueError("cannot replace an already received output")
    raw = Image.open(directory / "raw-output.png").convert("RGBA")
    if raw.width != raw.height:
        raise ValueError("non-square output needs explicit registration review")
    crop = entry["crop"]
    if args.name != "combined" and (crop is None or crop[2] - crop[0] != crop[3] - crop[1] or not 0 <= crop[0] < crop[2] <= 256 or not 0 <= crop[1] < crop[3] <= 256):
        raise ValueError("invalid retained crop transform")
    size = 256 if args.name == "combined" else crop[2] - crop[0]
    mapped = on_white(raw).resize((size, size), Image.Resampling.NEAREST).convert("RGBA")
    mapped.save(directory / "canvas-mapped.png")
    if args.name == "combined":
        source = Image.open(args.output / "source-rgba.png").convert("RGBA")
        mask = Image.open(directory / "mask.png").convert("L")
        composite = Image.composite(mapped, on_white(source).convert("RGBA"), mask)
        composite.save(directory / "composite.png")
        part = extract_white_matte(composite, source)
        # Keep the original alpha/RGB outside the editing region too.
        part = Image.composite(part, source, mask)
    else:
        guide = Image.open(directory / "source-part.png").convert("RGBA")
        part = extract_white_matte(mapped, guide)
        part.save(directory / "part-crop-rgba.png")
        global_part = Image.new("RGBA", (256, 256))
        global_part.paste(part, tuple(crop[:2]))
        part = global_part
    part.save(directory / "registered-rgba.png")
    entry.update(status="complete", raw_size=list(raw.size), raw_alpha_extrema=raw.getchannel("A").getextrema())
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Retained {args.name}: {raw.size}; fixed whole-canvas registration.")


def compose(args):
    manifest_path = args.output / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if {entry["name"] for entry in manifest["entries"]} != {"combined", "near", "far"}:
        raise ValueError("composition requires both independently generated legs")
    if any(entry["status"] != "complete" for entry in manifest["entries"]):
        raise ValueError("all three outputs must be retained first")
    if (args.output / "separate-composite.png").exists():
        raise ValueError("composite already exists")
    result = Image.new("RGBA", (256, 256))
    legs = Image.new("RGBA", (256, 256))
    for name in manifest["composite_order"]:
        path = args.output / name / "registered-rgba.png" if name in ("near", "far") else args.output / (name + ".png")
        image = Image.open(path).convert("RGBA")
        result = Image.alpha_composite(result, image)
        if name in ("far", "near"):
            legs = Image.alpha_composite(legs, image)
    result.save(args.output / "separate-composite.png")
    legs.save(args.output / "separate-legs.png")
    result.resize((48, 48), Image.Resampling.NEAREST).save(args.output / "separate-native-48.png")
    combined = Image.open(args.output / "combined/registered-rgba.png")
    combined.resize((48, 48), Image.Resampling.NEAREST).save(args.output / "combined-native-48.png")
    manifest["composition"] = {"status": "complete", "post_generation_fitting": False,
                                "method": "far leg, then near leg, then original coat and remaining parts; see explicit order"}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print("Composited independently generated legs in the declared order; no fitting or generation.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    prep = sub.add_parser("prepare")
    for name in ("ownership", "guides", "identity", "render", "output"):
        prep.add_argument("--" + name, type=Path, required=True)
    prep.add_argument("--frame", choices=("reference_10", "reference_04"), default="reference_10")
    prep.add_argument("--combined-only", action="store_true")
    rec = sub.add_parser("receive")
    rec.add_argument("--output", type=Path, required=True)
    rec.add_argument("--raw", type=Path, required=True)
    rec.add_argument("--name", choices=("combined", "near", "far"), required=True)
    comp = sub.add_parser("compose")
    comp.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    {"prepare": prepare, "receive": receive, "compose": compose}[args.action](args)


if __name__ == "__main__":
    main()
