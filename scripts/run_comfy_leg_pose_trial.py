"""Run one reviewed isolated-leg graph and retain the raw output, without fitting."""

import argparse
import json
import time
import urllib.parse
import uuid
from pathlib import Path

from PIL import Image

if __package__:
    from .run_comfy_gap_trial import request, sha, upload, write_json
else:
    from run_comfy_gap_trial import request, sha, upload, write_json


def reviewed_inputs(trial, name):
    manifest = json.loads((trial / "manifest.json").read_text())
    job = next((job for job in manifest["jobs"] if job["name"] == name and job["provider"] == "derry ComfyUI"), None)
    if job is None:
        raise ValueError("not a declared ComfyUI trial")
    graph_path = trial / job["graph"]
    if graph_path.parent.resolve() != trial.resolve():
        raise ValueError("graph must belong to this trial")
    data = graph_path.read_bytes()
    if sha(data) != manifest["file_sha256"].get(job["graph"]):
        raise ValueError("graph changed after input review")
    graph, paths = json.loads(data), {}
    prefix = manifest["remote_asset_prefix"] + "/"
    for node in graph.values():
        if node["class_type"] != "LoadImage":
            continue
        remote = node["inputs"]["image"]
        if not remote.startswith(prefix):
            raise ValueError("graph references another trial")
        relative = Path(remote[len(prefix):])
        path = trial / relative
        if relative.is_absolute() or len(relative.parts) != 1 or not path.is_file():
            raise ValueError("invalid reviewed asset path")
        if sha(path.read_bytes()) != manifest["file_sha256"].get(relative.as_posix()):
            raise ValueError(f"input changed after review: {relative}")
        paths[remote] = (path, relative)
    return manifest, job, graph, sha(data), paths


def run(args):
    if urllib.parse.urlparse(args.server).hostname not in ("127.0.0.1", "localhost"):
        raise ValueError("use the local SSH tunnel")
    manifest, job, graph, digest, paths = reviewed_inputs(args.trial, args.name)
    output = args.trial / "results" / args.name
    receipt_path = output / "receipt.json"
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else None
    if receipt:
        if receipt["reviewed_graph_sha256"] != digest:
            raise ValueError("cannot replace a submitted graph")
        if receipt["status"] == "complete":
            print("Already complete; nothing resubmitted.")
            return
        if receipt["status"] not in ("uploading", "running"):
            raise ValueError("previous submission is ambiguous or failed; reconcile it before retrying")
        if receipt["status"] == "running" and not receipt.get("prompt_id"):
            raise ValueError("running receipt lacks a prompt ID")
    if not receipt or receipt["status"] == "uploading":
        queue = request(args.server, "/queue")
        if queue["queue_running"] or queue["queue_pending"]:
            raise ValueError("ComfyUI is busy; keep measurements separate")
        output.mkdir(parents=True, exist_ok=True)
        receipt = {"status": "uploading", "reviewed_graph_sha256": digest, "client_id": str(uuid.uuid4()),
                   "input_sha256": {name: sha(path.read_bytes()) for name, (path, _) in paths.items()}, "uploads": {}}
        write_json(receipt_path, receipt)
        for name, (path, relative) in paths.items():
            receipt["uploads"][name] = upload(args.server, path, relative, manifest["remote_asset_prefix"])
            write_json(receipt_path, receipt)
        for node in graph.values():
            if node["class_type"] == "LoadImage":
                node["inputs"]["image"] = receipt["uploads"][node["inputs"]["image"]]
        write_json(output / "submitted-graph.json", graph)
        receipt.update(status="submitting", submitted_at_epoch=time.time(), system_before=request(args.server, "/system_stats"))
        write_json(receipt_path, receipt)
        job["status"] = "submitting; reconcile receipt if interrupted"
        manifest["status"] = "generation submission in progress; inspect job receipts"
        write_json(args.trial / "manifest.json", manifest)
        # Persist submitting BEFORE POST. An uncertain result must never trigger a blind retry.
        ack = request(args.server, "/prompt", {"prompt": graph, "client_id": receipt["client_id"]})
        if not ack.get("prompt_id") or ack.get("node_errors"):
            receipt.update(status="rejected", error=ack)
            write_json(receipt_path, receipt)
            raise ValueError(f"ComfyUI rejected the graph: {ack}")
        receipt.update(status="running", prompt_id=ack["prompt_id"])
        write_json(receipt_path, receipt)
        job.update(status="running", prompt_id=ack["prompt_id"])
        manifest["status"] = "generation in progress; inspect job receipts"
        manifest["provider_calls"] = sum(bool(item.get("prompt_id") or item.get("raw_source")) for item in manifest["jobs"])
        write_json(args.trial / "manifest.json", manifest)
        print(f"Submitted {args.name}: {ack['prompt_id']}", flush=True)
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        history = request(args.server, "/history/" + receipt["prompt_id"])
        item = history.get(receipt["prompt_id"])
        if not item:
            time.sleep(2)
            continue
        write_json(output / "history.json", item)
        if item["status"]["status_str"] != "success":
            receipt.update(status="execution_failed", error=item["status"])
            write_json(receipt_path, receipt)
            raise RuntimeError(str(item["status"]))
        images = item["outputs"]["16"]["images"]
        if len(images) != 1:
            raise ValueError("expected exactly one raw output")
        raw = request(args.server, "/view?" + urllib.parse.urlencode(images[0]), raw=True)
        (output / "raw-output.png").write_bytes(raw)
        with Image.open(output / "raw-output.png") as image:
            size = image.size
            if size != tuple(manifest["requested_canvas"]):
                raise ValueError("unexpected output dimensions; inspect retained raw image before mapping")
            crop = manifest["fixed_crop"]
            mapped = image.convert("RGB").resize((crop[2] - crop[0], crop[3] - crop[1]), Image.Resampling.NEAREST)
            mapped.save(output / "fixed-crop-rgb.png")
        receipt.update(status="complete", elapsed_seconds=time.time() - receipt["submitted_at_epoch"],
                       raw_sha256=sha(raw), raw_size=list(size), post_generation_fitting=False)
        write_json(receipt_path, receipt)
        job.update(status="complete", prompt_id=receipt["prompt_id"], raw_sha256=sha(raw))
        manifest["provider_calls"] = sum(bool(item.get("prompt_id") or item.get("raw_source")) for item in manifest["jobs"])
        manifest["status"] = "results need evaluation; see individual job status"
        write_json(args.trial / "manifest.json", manifest)
        print(f"Retained {args.name}: raw {size}, fixed crop only.", flush=True)
        return
    raise TimeoutError("job is pending; resume this trial to poll the same prompt ID")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", default="http://127.0.0.1:8189")
    parser.add_argument("--trial", type=Path, required=True)
    parser.add_argument("--name", choices=("canny-0.65", "canny-0.85"), required=True)
    parser.add_argument("--timeout", type=float, default=900)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
