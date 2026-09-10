"""Summarize the completed gap control and show unsubmitted boot-view guides."""

import argparse
import json
from pathlib import Path

from PIL import Image

from render_gap_trial_review import delta_url
from render_puppet_review import png_url


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("results", "inputs", "guides", "document", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    data, measurements = {"results": {}, "guides": []}, []
    for frame in ("reference_05", "reference_10"):
        source = args.inputs / frame / "blank.png"
        region = Image.open(args.inputs / frame / "missing-region.png").convert("L")
        record = {"base": png_url(source), "variants": {}}
        for variant in ("blank", "blur", "shaped"):
            inp = Image.open(args.inputs / frame / (variant + ".png")).convert("RGB")
            item = {"input": delta_url(source, args.inputs / frame / (variant + ".png"))}
            for denoise in ("0.35", "0.60"):
                directory = args.results / (frame + "-" + variant + "-" + denoise)
                receipt = json.loads((directory / "receipt.json").read_text())
                if receipt["status"] != "complete":
                    raise ValueError("trial is not complete")
                working = Image.open(directory / "composite.png").convert("RGB").resize((256, 256), Image.Resampling.NEAREST)
                working.save(directory / "working.png")
                native = working.resize((48, 48), Image.Resampling.NEAREST)
                native.save(directory / "native-48.png")
                item[denoise] = delta_url(source, directory / "working.png")
                white = lambda im: sum(m > 0 and min(rgb) >= 225 and max(rgb) - min(rgb) <= 16 for m, rgb in zip(region.getdata(), im.getdata(), strict=True))
                changed = sum(a != b for a, b in zip(inp.resize((48, 48), Image.Resampling.NEAREST).getdata(), native.getdata(), strict=True))
                measurements.append({"frame": frame, "variant": variant, "denoise": float(denoise),
                                     "white_region_before": white(inp), "white_region_after": white(working),
                                     "changed_pixels_at_48": changed, "seconds": receipt["elapsed_seconds"],
                                     "outside_mask_changed_pixels": receipt["outside_mask_changed_pixels"]})
            record["variants"][variant] = item
        data["results"][frame] = record
    guide_manifest = json.loads((args.guides / "manifest.json").read_text())
    poses = {frame["name"]: frame["pose"] for frame in json.loads(args.document.read_text())["frames"]}
    for frame in guide_manifest["frames"]:
        directory = args.guides / frame["name"]
        source = directory / "original.png"
        data["guides"].append({"name": frame["name"], "base": png_url(source),
                               "geometry": delta_url(source, directory / "geometry.png"),
                               "surfaces": delta_url(source, directory / "surfaces.png"),
                               "mask": png_url(directory / "mask.png"), "annotations": frame["annotations"],
                               "pose": poses[frame["name"]]})
    summary = {"provider": "local ComfyUI on derry RTX 3090", "generation_calls": len(measurements),
               "seconds_total": sum(row["seconds"] for row in measurements), "measurements": measurements,
               "metric_limit": "Near-white pixels only inside proposed missing regions; not an anatomy score. Native changes use a fixed whole-canvas 1024→256→48 nearest-neighbor mapping.",
               "decision": "No full cycle: prefills supply the connection while this local repair contributes little at 48px; boots remain fixed by the control mask. Proceed to reviewed full-leg/boot guides."}
    (args.results / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    template = Path(__file__).with_name("boot_experiments_review.html").read_text()
    result = template.replace("<!-- BOOT_EXPERIMENT_DATA -->", json.dumps(data, separators=(",", ":")).replace("</", "<\\/"))
    if len(result.encode()) >= 1_000_000:
        raise ValueError("inline review exceeds size limit")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print(f"Reviewed 12 completed calls; {summary['seconds_total']:.1f}s total; HTML {len(result.encode())} bytes.")


if __name__ == "__main__":
    main()
