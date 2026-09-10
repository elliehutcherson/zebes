"""Show four retained redraws and distinguish the two compositing policies."""

import argparse
import json
from pathlib import Path

from render_gap_trial_review import delta_url
from render_puppet_review import png_url


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trial", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.trial / "manifest.json").read_text())
    data = []
    for frame in manifest["frames"]:
        if frame["status"] != "complete" or "wider_comparison" not in frame:
            raise ValueError("trial and compositing comparison must be complete")
        directory = args.trial / frame["name"]
        base = directory / "original.png"
        data.append({"name": frame["name"], "base": png_url(base),
                     "guide": delta_url(base, directory / "geometry.png"),
                     "raw": png_url(directory / "canvas-mapped.png"),
                     "original": delta_url(base, directory / "composite.png"),
                     "broader": delta_url(base, directory / "wide-composite.png"),
                     "original_mask": png_url(directory / "repair-mask.png"),
                     "broader_mask": png_url(directory / "wide-mask.png")})
    template = Path(__file__).with_name("stout_boot_review.html").read_text()
    result = template.replace("<!-- STOUT_BOOT_DATA -->", json.dumps(data, separators=(",", ":")).replace("</", "<\\/"))
    if len(result.encode()) >= 1_000_000:
        raise ValueError("review exceeds inline size limit")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print(f"Wrote four-pose review, {len(result.encode())} bytes.")


if __name__ == "__main__":
    main()
