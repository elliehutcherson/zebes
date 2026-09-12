"""Render the twelve source poses beside their traced leg chains.

This diagnostic makes no provider calls and does not invent heel positions.
"""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


PHASES = (
    "near contact", "near loading", "near mid-support", "near late support", "near toe-off", "flight",
    "far contact", "far loading", "far mid-support", "far late support", "far toe-off", "flight",
)
COLORS = {"near": (231, 119, 42, 230), "far": (42, 139, 205, 230)}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(sheet_path, trace_path, output):
    if output.exists():
        raise ValueError("output exists; preserve the previous phase audit")
    trace = json.loads(trace_path.read_text())
    frames = trace["frames"]
    if len(frames) != 12:
        raise ValueError("run phase audit requires exactly twelve frames")
    if [frame["support"] for frame in frames] != ["near"] * 5 + ["flight"] + ["far"] * 5 + ["flight"]:
        raise ValueError("trace support sequence no longer matches the reviewed run cycle")
    sheet = Image.open(sheet_path).convert("RGB")
    font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    font = ImageFont.truetype(str(font_path), 18) if font_path.exists() else ImageFont.load_default()
    small = ImageFont.truetype(str(font_path), 14) if font_path.exists() else font
    panel_size, image_size = (420, 350), (200, 276)
    board = Image.new("RGB", (panel_size[0] * 6, panel_size[1] * 2 + 62), (242, 241, 236))
    board_draw = ImageDraw.Draw(board)
    records = []
    for index, (frame, phase) in enumerate(zip(frames, PHASES, strict=True)):
        left, top, width, height = frame["cell"]
        source = sheet.crop((left, top, left + width, top + height))
        overlay = source.convert("RGBA")
        draw = ImageDraw.Draw(overlay)
        pose = frame["pose"]
        paths = {}
        for side, suffix in (("near", "l"), ("far", "r")):
            path = [pose["hip_c"], pose["knee_" + suffix], pose["ankle_" + suffix], pose["toe_" + suffix]]
            paths[side] = path
            draw.line([tuple(point) for point in path], fill=COLORS[side], width=4, joint="curve")
            for point in path:
                x, y = point
                draw.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(255, 255, 255, 245), outline=COLORS[side], width=2)
        x = (index % 6) * panel_size[0]
        y = (index // 6) * panel_size[1]
        board_draw.text((x + 8, y + 6), f"{index + 1}. {phase}", fill=(24, 28, 32), font=font)
        board.paste(source.resize(image_size, Image.Resampling.LANCZOS), (x + 5, y + 36))
        board.paste(overlay.convert("RGB").resize(image_size, Image.Resampling.LANCZOS), (x + 215, y + 36))
        if index == 9:
            board_draw.rectangle((x + 2, y + 2, x + 417, y + 345), outline=(188, 48, 48), width=4)
            board_draw.text((x + 218, y + 316), "near recovery: heel-up intent", fill=(150, 35, 35), font=small)
        records.append({
            "name": frame["name"], "phase": phase, "support": frame["support"], "paths": paths,
            "explicit_heel_landmark": False,
        })
    board_draw.text((8, 708), "Left: unchanged source drawing. Right: traced chains — orange near, blue far. Points: hip, knee, ankle, toe.", fill=(24, 28, 32), font=font)
    board_draw.text((8, 736), "Frame 10: far leg supports; near leg recovers with ankle above toe. No heel point is currently traced.", fill=(150, 35, 35), font=font)
    output.mkdir(parents=True)
    board.save(output / "review.png")
    manifest = {
        "status": "phase audit prepared; no generation inputs and no provider calls",
        "provider_calls": 0,
        "source_sheet": str(sheet_path),
        "source_sheet_sha256": digest(sheet_path),
        "source_trace": str(trace_path),
        "source_trace_sha256": digest(trace_path),
        "phase_model": "five stance drawings then flight, repeated with leg ownership swapped",
        "limitation": "The source trace records ankle and toe, but no heel or sole-contact segment.",
        "frames": records,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "README.md").write_text(
        "# Twelve-frame run phase audit\n\n"
        "This is a diagnostic of the unchanged supplied pose sheet and retained trace. It makes no provider calls.\n\n"
        "Frames 1–5 use the near leg for contact through toe-off, frame 6 is flight, frames 7–11 repeat the stance phases with the far leg, and frame 12 is flight. Frame 10 is therefore far late-support with the near leg folded in recovery. Its traced near chain runs knee `(141,201)` to ankle `(99,184)` to toe `(86,199)` in source-cell pixels: the ankle is higher than the toe, matching a raised-heel side-profile foot.\n\n"
        "The trace has no heel landmark or sole-contact segment. The rejected boot proxy inferred a 3D boot from that incomplete representation and used an oblique camera. Do not use it for another generation request. Add and review explicit heel/sole semantics first.\n"
    )
    print("Prepared twelve-frame source/trace phase audit; no provider calls.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sheet", required=True, type=Path)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.sheet, args.trace, args.output)


if __name__ == "__main__":
    main()
