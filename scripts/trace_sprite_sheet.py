#!/usr/bin/env python3
"""Serve the sprite trace editor on loopback so joints are placed by hand.

The Samus trace this replaces was read off 42px sprites by eye, and roughly half
of it was inference presented as measurement: a far arm drawn down the middle of
the chest where no arm is painted, a far toe landing in empty background, knees
at mid-thigh. The fix is not a steadier eye, it is letting the person who can see
the artwork place the joints and say which ones they actually saw.

Every joint therefore carries a confidence of "observed" or "estimated". A trace
that claims a joint without saying which is rejected rather than written, because
an unmarked guess is exactly the failure this tool exists to end.

Run it, open the printed address, correct the frames, press save:

    scripts/trace_sprite_sheet.py \
        --sheet experiments/pose_analogy/inputs/reference-run-10.png \
        --trace experiments/pose_analogy/inputs/reference-run-trace-v1.json
"""

import argparse
import json
import shutil
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

JOINTS = ["hip_c", "neck", "head_top",
          "shoulder_l", "elbow_l", "wrist_l", "paw_l",
          "shoulder_r", "elbow_r", "wrist_r", "paw_r",
          "knee_l", "ankle_l", "toe_l", "knee_r", "ankle_r", "toe_r"]
SUPPORTS = {"near", "far", "flight"}
CONFIDENCE = {"observed", "estimated"}
MAXIMUM_BODY = 8 * 1024 * 1024


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_size(path):
    """Width and height from the PNG header.

    Reading eight bytes of IHDR keeps this tool on the standard library, so it
    runs under whatever python3 is on PATH. Pillow lives only in the build venv,
    and a tracing tool that cannot start without a built tree is a tool nobody
    reaches for.
    """
    header = path.read_bytes()[:24]
    if len(header) < 24 or header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
        raise ValueError(f"{path} is not a PNG")
    width = int.from_bytes(header[16:20], "big")
    height = int.from_bytes(header[20:24], "big")
    if width <= 0 or height <= 0:
        raise ValueError(f"{path} reports an empty image")
    return width, height


def scaffold(sheet_size, count, cell):
    width, height = cell
    return {
        "version": 1,
        "source_size": list(sheet_size),
        "cell_size": list(cell),
        "notes": "Traced by hand in scripts/sprite_trace_editor.html.",
        "ground_y": height - 1,
        "frames": [{"name": f"reference_{index + 1:02d}",
                    "cell": [width * index, 0, width, height],
                    "support": "flight", "pose": {}, "confidence": {}}
                   for index in range(count)],
    }


def validate(trace, sheet_size):
    """Reject anything a later stage would have to guess about."""
    if not isinstance(trace, dict):
        raise ValueError("the trace must be an object")
    frames = trace.get("frames")
    if not isinstance(frames, list) or not frames:
        raise ValueError("the trace needs at least one frame")
    sheet_width, sheet_height = sheet_size
    names = set()
    for frame in frames:
        name = frame.get("name")
        if not name or name in names:
            raise ValueError(f"frames need unique names, saw {name!r} twice")
        names.add(name)
        if frame.get("support") not in SUPPORTS:
            raise ValueError(f"{name}: support must be one of {sorted(SUPPORTS)}")
        cell = frame.get("cell")
        if not (isinstance(cell, list) and len(cell) == 4):
            raise ValueError(f"{name}: cell must be [x, y, width, height]")
        x, y, width, height = cell
        if width <= 0 or height <= 0:
            raise ValueError(f"{name}: cell must have a positive size")
        if x < 0 or y < 0 or x + width > sheet_width or y + height > sheet_height:
            raise ValueError(f"{name}: cell {cell} falls outside the {sheet_width}x{sheet_height} sheet")
        pose, confidence = frame.get("pose", {}), frame.get("confidence", {})
        missing = [joint for joint in JOINTS if joint not in pose]
        if missing:
            raise ValueError(f"{name}: still unplaced: {', '.join(missing)}")
        extra = sorted(set(pose) - set(JOINTS))
        if extra:
            raise ValueError(f"{name}: unknown joints: {', '.join(extra)}")
        for joint in JOINTS:
            point = pose[joint]
            if not (isinstance(point, list) and len(point) == 2):
                raise ValueError(f"{name}/{joint}: expected [x, y]")
            if not all(isinstance(value, (int, float)) for value in point):
                raise ValueError(f"{name}/{joint}: coordinates must be numbers")
            if not (0 <= point[0] < width and 0 <= point[1] < height):
                raise ValueError(f"{name}/{joint}: {point} falls outside its own cell")
            if confidence.get(joint) not in CONFIDENCE:
                raise ValueError(f"{name}/{joint}: mark it observed or estimated")
    supports = [frame["support"] for frame in frames]
    if all(support == "flight" for support in supports):
        raise ValueError("no frame has a foot on the ground, so nothing can be grounded")
    for frame in frames:
        conflict = support_conflict(frame)
        if conflict:
            raise ValueError(conflict)
    return trace


def lowest_point(pose, side):
    """How far down a leg reaches, measured at whichever of ankle or toe is lower.

    Comparing toes alone would call a heel strike an error, because a planted foot
    can hold its toe above the swinging foot's.
    """
    return max(pose["ankle_" + side][1], pose["toe_" + side][1])


def support_conflict(frame):
    """The named support foot must be the one actually reaching furthest down.

    Grounding seats the named foot on the ground row and moves the whole body to
    do it. Name the raised foot and the other leg is driven through the floor:
    reference_01 labelled far while its near foot sat ten source pixels lower,
    and the transferred mouse put its near toe fourteen pixels below ground.
    """
    support = frame["support"]
    if support == "flight":
        return None
    pose = frame["pose"]
    named, other = ("l", "r") if support == "near" else ("r", "l")
    reach, rival = lowest_point(pose, named), lowest_point(pose, other)
    if reach >= rival:
        return None
    return (f"{frame['name']}: support is {support}, but that foot reaches row {reach:g} "
            f"while the other reaches {rival:g}. Either flip the support or swap the legs.")


def page_bytes(page):
    return page.read_bytes()


def build_handler(sheet, trace_path, page, sheet_size):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def send(self, code, body, content_type):
            self.send_response(code)
            self.send_header("content-type", content_type)
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            route = self.path.split("?")[0].lstrip("/")
            if route in ("", "index.html"):
                self.send(200, page_bytes(page), "text/html; charset=utf-8")
            elif route == "sheet.png":
                self.send(200, sheet.read_bytes(), "image/png")
            elif route == "trace":
                if trace_path.is_file():
                    body = trace_path.read_bytes()
                else:
                    body = json.dumps(scaffold(sheet_size, 1, sheet_size)).encode()
                self.send(200, body, "application/json; charset=utf-8")
            else:
                self.send(404, b"no such route", "text/plain; charset=utf-8")

        def do_POST(self):
            if self.path.split("?")[0].lstrip("/") != "trace":
                self.send(404, b"no such route", "text/plain; charset=utf-8")
                return
            length = int(self.headers.get("content-length") or 0)
            if length <= 0 or length > MAXIMUM_BODY:
                self.send(413, b"body too large or empty", "text/plain; charset=utf-8")
                return
            try:
                trace = validate(json.loads(self.rfile.read(length)), sheet_size)
            except (ValueError, json.JSONDecodeError) as error:
                self.send(400, f"not saved: {error}".encode(), "text/plain; charset=utf-8")
                return
            if trace_path.is_file():
                shutil.copyfile(trace_path, trace_path.with_suffix(trace_path.suffix + ".bak"))
            trace_path.parent.mkdir(parents=True, exist_ok=True)
            trace_path.write_text(json.dumps(trace, indent=2) + "\n")
            guessed = sum(1 for frame in trace["frames"]
                          for value in frame["confidence"].values() if value == "estimated")
            total = len(trace["frames"]) * len(JOINTS)
            self.send(200, f"saved {trace_path.name}: {len(trace['frames'])} frames, "
                           f"{guessed}/{total} joints marked guessed".encode(),
                      "text/plain; charset=utf-8")

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--sheet", type=Path,
                        default=root / "experiments/pose_analogy/inputs/reference-run-10.png")
    parser.add_argument("--trace", type=Path,
                        default=root / "experiments/pose_analogy/inputs/reference-run-trace-v1.json")
    parser.add_argument("--page", type=Path, default=root / "scripts/sprite_trace_editor.html")
    parser.add_argument("--port", type=int, default=8771)
    args = parser.parse_args()
    for path in [args.sheet, args.page]:
        if not path.is_file():
            raise SystemExit(f"missing {path}")

    sheet_size = png_size(args.sheet)
    server = ThreadingHTTPServer(
        ("127.0.0.1", args.port),
        build_handler(args.sheet, args.trace, args.page, sheet_size))
    # serve_forever never returns, so an unflushed address line is an address the
    # user never sees whenever stdout is a pipe rather than a terminal.
    print(f"tracing {args.sheet.name} ({sheet_size[0]}x{sheet_size[1]}) into {args.trace}",
          flush=True)
    print(f"open http://127.0.0.1:{args.port}/  (ctrl-c to stop)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
