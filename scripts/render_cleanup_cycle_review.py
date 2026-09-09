"""Publish a complete cleanup-cycle comparison without further generation."""

import argparse
import base64
import io
import json
from pathlib import Path

from PIL import Image


VARIANTS = {"source": "source.png", "model": "matte-extracted.png",
            "composite": "masked-composite-extracted.png"}


def image_url(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.root / "manifest.json").read_text())
    frames = manifest["frames"]
    if len(frames) != 12 or manifest["provider_calls"] != 12:
        raise ValueError("publish only one complete twelve-frame batch")
    data = {"fps": 12, "frames": []}
    images = {key: [] for key in VARIANTS}
    for frame in frames:
        record = {"name": frame["name"]}
        for variant, filename in VARIANTS.items():
            image = Image.open(args.root / frame["name"] / filename).convert("RGBA")
            if image.size != (256, 256):
                raise ValueError("comparison requires shared 256px canvases")
            native = image.resize((48, 48), Image.Resampling.NEAREST)
            detail = image.resize((128, 128), Image.Resampling.NEAREST)
            record[variant] = {"native": image_url(native), "detail": image_url(detail)}
            images[variant].append(native)
        data["frames"].append(record)
    for variant, sequence in images.items():
        sheet = Image.new("RGBA", (288, 96))
        for index, image in enumerate(sequence):
            sheet.paste(image, ((index % 6) * 48, (index // 6) * 48))
        sheet.save(args.root / (variant + "-sheet.png"))
        # APNG retains RGBA pixels, a shared canvas and all twelve ordered frames.
        durations = [round((index + 1) * 1000 / 12) - round(index * 1000 / 12) for index in range(12)]
        sequence[0].save(args.root / (variant + "-loop.png"), save_all=True,
                         append_images=sequence[1:], duration=durations,
                         loop=0, disposal=0, blend=0)
    template = Path(__file__).with_name("pose_cleanup_review.html").read_text()
    fragment = template.replace("<!-- CLEANUP_DATA -->", json.dumps(data, separators=(",", ":")))
    if len(fragment.encode()) >= 1_000_000:
        raise ValueError("comparison exceeds the inline size budget")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(fragment)
    print(f"Wrote {args.output}: {len(fragment.encode())} bytes")


if __name__ == "__main__":
    main()
