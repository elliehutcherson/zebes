"""Build a self-contained review from actual layered-puppet renders.

Run with build/tileset-venv/bin/python. The layer-order image is an ordinal
compositing diagnostic, not estimated physical depth or a generator input.
"""

import argparse
import base64
import io
import json
from pathlib import Path

from PIL import Image


def png_url(path):
    # Recompress without resizing or quantizing; the decoded pixels stay exact.
    buffer = io.BytesIO()
    with Image.open(path) as image:
        if image.mode == "RGBA" and image.getchannel("A").getextrema() == (255, 255):
            image = image.convert("RGB")
        if image.mode == "RGB":
            red, green, blue = image.split()
            if red.tobytes() == green.tobytes() == blue.tobytes():
                image = red
        image.save(buffer, format="PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()


def layer_order_url(render_root, frame, size):
    result = Image.new("L", size, 0)
    for index, part in enumerate(frame["draw_order"]):
        path = render_root / "part-poses" / part / (frame["name"] + ".png")
        with Image.open(path) as image:
            if image.size != size:
                raise ValueError(f"part canvas disagrees with document: {path}")
            mask = image.convert("RGBA").getchannel("A")
            value = 40 + round(215 * index / max(1, len(frame["draw_order"]) - 1))
            result.paste(value, mask=mask)
    buffer = io.BytesIO()
    result.save(buffer, format="PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--before-document", required=True, type=Path)
    parser.add_argument("--document", required=True, type=Path)
    parser.add_argument("--before-render", required=True, type=Path)
    parser.add_argument("--render", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--pose-sheet", type=Path)
    parser.add_argument("--ground-y", type=int, default=214)
    args = parser.parse_args()
    document = json.loads(args.document.read_text())
    before = json.loads(args.before_document.read_text())
    size = (document["width"], document["height"])
    with Image.open(args.source) as source:
        if source.size != size:
            raise ValueError("source dimensions disagree with document")
    names = [frame["name"] for frame in document["frames"]]
    if len(names) != 12 or names != [frame["name"] for frame in before["frames"]]:
        raise ValueError("review requires the same twelve ordered phases")
    data = {
        "source": png_url(args.source),
        "before": before["rest_pose"],
        "rest": document["rest_pose"],
        "bones": document["bones"],
        "width": size[0],
        "height": size[1],
        "fps": document["fps"],
        "ground_y": args.ground_y,
        "frames": [],
    }
    trace = None
    if args.trace:
        if args.pose_sheet is None:
            raise ValueError("a trace requires its actual pose sheet")
        trace = json.loads(args.trace.read_text())
        if names != [frame["name"] for frame in trace["frames"]]:
            raise ValueError("trace order differs from the rendered clip")
        data["guide"] = png_url(args.pose_sheet)
        data["guide_size"] = trace["source_size"]
        data["guide_bones"] = trace["bones"]
    for frame in document["frames"]:
        filename = frame["name"] + ".png"
        data["frames"].append({
            "name": frame["name"],
            "pose": frame["pose"],
            "working": png_url(args.render / "working" / filename),
            "native": png_url(args.render / "frames" / filename),
            "before": png_url(args.before_render / "frames" / filename),
            "order": layer_order_url(args.render, frame, size),
        })
        if trace:
            data["frames"][-1]["trace"] = trace["frames"][len(data["frames"]) - 1]
    template = Path(__file__).with_name("puppet_review.html").read_text()
    encoded = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    result = template.replace("<!-- PUPPET_REVIEW_DATA -->", encoded)
    if len(result.encode()) >= 1_000_000:
        raise ValueError("review exceeds the inline 1 MB size budget")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print(f"Wrote twelve-frame review: {args.output} ({len(result.encode())} bytes)")


if __name__ == "__main__":
    main()
