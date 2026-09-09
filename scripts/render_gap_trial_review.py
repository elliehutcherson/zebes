"""Build an offline review of exact prefills and the external run benchmark."""

import argparse
import base64
import io
import json
from pathlib import Path

from PIL import Image, ImageChops

from prepare_run_gap_trial import maximum
from render_puppet_review import png_url


def delta_url(base_path, target_path):
    base = Image.open(base_path).convert("RGB")
    target = Image.open(target_path).convert("RGB")
    mask = maximum(list(ImageChops.difference(base, target).split())).point(lambda value: 255 if value else 0)
    delta = Image.new("RGBA", base.size)
    delta.paste(target.convert("RGBA"), mask=mask)
    reconstructed = Image.alpha_composite(base.convert("RGBA"), delta).convert("RGB")
    if reconstructed.tobytes() != target.tobytes():
        raise ValueError("compact preview does not reproduce the input")
    buffer = io.BytesIO()
    delta.save(buffer, format="PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefills", type=Path, required=True)
    parser.add_argument("--document", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--spine", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    document = json.loads(args.document.read_text())
    manifest = json.loads((args.prefills / "manifest.json").read_text())
    data = {"source": png_url(args.source), "rest": document["rest_pose"], "bones": document["bones"],
            "config": json.loads(args.config.read_text()), "trace": json.loads(args.trace.read_text()),
            "spine": json.loads(args.spine.read_text()), "frames": []}
    for frame, recorded in zip(document["frames"], manifest["frames"], strict=True):
        if frame["name"] != recorded["name"]:
            raise ValueError("review frame order differs")
        directory = args.prefills / frame["name"]
        base = directory / "blank.png"
        data["frames"].append({"name": frame["name"], "pose": frame["pose"], "base": png_url(base),
                               "blur": delta_url(base, directory / "blur.png"),
                               "shaped": delta_url(base, directory / "shaped.png"),
                               "mask": png_url(directory / "repair-mask.png"),
                               "annotations": recorded["annotations"]})
    template = Path(__file__).with_name("gap_trial_review.html").read_text()
    result = template.replace("<!-- GAP_TRIAL_DATA -->", json.dumps(data, separators=(",", ":")).replace("</", "<\\/"))
    if len(result.encode()) >= 1_000_000:
        raise ValueError("review exceeds inline size limit")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print(f"Wrote exact-pixel input review ({len(result.encode())} bytes).")


if __name__ == "__main__":
    main()
