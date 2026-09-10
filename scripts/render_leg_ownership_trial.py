"""Build the comparison from retained model outputs, with no generation."""

import argparse
import base64
import io
import json
from pathlib import Path

from PIL import Image


def image_url(path, size=None):
    image = Image.open(path).convert("RGBA")
    image = Image.alpha_composite(Image.new("RGBA", image.size, "white"), image).convert("RGB")
    if size:
        image = image.resize(size, Image.Resampling.NEAREST)
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("trial", "opposite", "previous", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    data = {
        "comparison": [image_url(args.previous), image_url(args.trial / "combined/registered-rgba.png"), image_url(args.trial / "separate-composite.png")],
        "parts": [image_url(args.trial / "near/source-part.png"), image_url(args.trial / "near/canvas-mapped.png"),
                  image_url(args.trial / "far/source-part.png"), image_url(args.trial / "far/canvas-mapped.png")],
        "opposite": [image_url(args.opposite / "combined/edit-input.png", (256, 256)), image_url(args.opposite / "combined/registered-rgba.png")],
    }
    template = Path(__file__).with_name("leg_ownership_trial_review.html").read_text()
    result = template.replace("<!-- OWNERSHIP_TRIAL_DATA -->", json.dumps(data, separators=(",", ":")))
    if len(result.encode()) >= 1_000_000:
        raise ValueError("review exceeds inline size limit")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print(f"Wrote leg-ownership comparison ({len(result.encode())} bytes).")


if __name__ == "__main__":
    main()
