"""Run one already-reviewed ComfyUI gap graph and retain its exact provenance.

Connect through an SSH loopback tunnel. Submission is never retried after an
ambiguous response. Re-running a directory with a prompt ID resumes that job.
"""

import argparse
import hashlib
import io
import json
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

from PIL import Image, ImageChops


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def request(server, route, payload=None, content_type="application/json", raw=False):
    data = None if payload is None else (payload if isinstance(payload, bytes) else json.dumps(payload).encode())
    req = urllib.request.Request(server + route, data=data)
    if data is not None:
        req.add_header("Content-Type", content_type)
    with urllib.request.urlopen(req, timeout=60) as response:
        result = response.read()
    return result if raw else json.loads(result)


def asset_path(root, remote_name):
    prefix = "zebes-run-gap-v1/"
    if not remote_name.startswith(prefix):
        raise ValueError("graph references an asset outside the reviewed trial")
    relative = Path(remote_name[len(prefix):])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("invalid asset path")
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError(f"missing reviewed input: {relative}")
    return path, relative


def upload(server, path, relative, remote_root="zebes-run-gap-control-v1"):
    boundary = "zebes_" + uuid.uuid4().hex
    body = bytearray()
    for name, value in {"type": "input", "subfolder": remote_root + "/" + str(relative.parent), "overwrite": "false"}.items():
        body.extend(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode())
    body.extend(f'--{boundary}\r\nContent-Disposition: form-data; name="image"; filename="{path.name}"\r\nContent-Type: image/png\r\n\r\n'.encode())
    body.extend(path.read_bytes())
    body.extend(f'\r\n--{boundary}--\r\n'.encode())
    result = request(server, "/upload/image", bytes(body), "multipart/form-data; boundary=" + boundary)
    if result.get("type") != "input" or not result.get("name"):
        raise ValueError(f"unexpected upload response: {result}")
    remote = str(Path(result.get("subfolder", "")) / result["name"])
    # Verify actual uploaded bytes before allowing them to reach the sampler.
    query = {"filename": result["name"], "subfolder": result.get("subfolder", ""), "type": "input"}
    downloaded = request(server, "/view?" + urllib.parse.urlencode(query), raw=True)
    if sha(downloaded) != sha(path.read_bytes()):
        raise ValueError("uploaded input differs from reviewed file")
    return remote


def validate_reviewed_inputs(graph, assets):
    manifest = json.loads((assets / "manifest.json").read_text())
    plan = json.loads((assets.parent / "workflows/plan.json").read_text())
    hashes = {"identity-1024.png": plan["identity_input_sha256"]}
    for frame in manifest["frames"]:
        hashes[frame["name"] + "/mask-1024.png"] = frame["mask_sha256"]
        for variant, digest in frame["inputs_sha256"].items():
            hashes[frame["name"] + "/" + variant + "-1024.png"] = digest
    paths = {}
    for node in graph.values():
        if node["class_type"] != "LoadImage":
            continue
        name = node["inputs"]["image"]
        path, relative = asset_path(assets, name)
        if sha(path.read_bytes()) != hashes.get(relative.as_posix()):
            raise ValueError(f"input changed after review: {relative}")
        paths[name] = (path, relative)
    return paths


def verify_composite(blank_bytes, raw_bytes, mask_bytes, server_bytes):
    blank = Image.open(io.BytesIO(blank_bytes)).convert("RGB")
    raw = Image.open(io.BytesIO(raw_bytes)).convert("RGB")
    mask = Image.open(io.BytesIO(mask_bytes)).convert("L")
    server = Image.open(io.BytesIO(server_bytes)).convert("RGB")
    if any(image.size != (1024, 1024) for image in (blank, raw, mask, server)):
        raise ValueError("the graph changed the reviewed 1024px canvas")
    composite = Image.composite(raw, blank, mask)
    outside = ImageChops.invert(mask)
    for channel in ImageChops.difference(blank, composite).split():
        if ImageChops.multiply(channel, outside).getbbox():
            raise ValueError("protected pixels changed in local composite")
    difference = ImageChops.difference(server, composite)
    max_difference = max(channel.getextrema()[1] for channel in difference.split())
    return composite, max_difference


def run(args):
    if urllib.parse.urlparse(args.server).hostname not in ("127.0.0.1", "localhost"):
        raise ValueError("use the local SSH tunnel, not a publicly exposed ComfyUI server")
    graph_bytes = args.graph.read_bytes()
    graph = json.loads(graph_bytes)
    paths = validate_reviewed_inputs(graph, args.assets)
    args.output.mkdir(parents=True, exist_ok=True)
    receipt_path = args.output / "receipt.json"
    if receipt_path.exists():
        receipt = json.loads(receipt_path.read_text())
        if receipt["reviewed_graph_sha256"] != sha(graph_bytes):
            raise ValueError("cannot replace a submitted graph")
        if receipt["status"] == "complete":
            print("Already complete; nothing resubmitted.", flush=True)
            return
        if not receipt.get("prompt_id") and receipt["status"] != "uploading":
            raise ValueError("existing attempt has no confirmed prompt ID; reconcile it before retrying")
    if not receipt_path.exists() or receipt["status"] == "uploading":
        queue = request(args.server, "/queue")
        if queue["queue_running"] or queue["queue_pending"]:
            raise ValueError("ComfyUI is busy; do not interleave this measurement with another job")
        receipt = {"status": "uploading", "reviewed_graph_sha256": sha(graph_bytes),
                   "client_id": str(uuid.uuid4()), "input_hashes": {}, "uploads": {},
                   "authorization": "user reviewed prefills and authorized continued experiments; connection-only control"}
        write_json(receipt_path, receipt)
        for name, (path, relative) in paths.items():
            receipt["uploads"][name] = upload(args.server, path, relative)
            receipt["input_hashes"][name] = sha(path.read_bytes())
            write_json(receipt_path, receipt)
        for node in graph.values():
            if node["class_type"] == "LoadImage":
                node["inputs"]["image"] = receipt["uploads"][node["inputs"]["image"]]
        write_json(args.output / "submitted-graph.json", graph)
        receipt["system_before"] = request(args.server, "/system_stats")
        receipt["status"] = "submitting"
        receipt["submitted_at_epoch"] = time.time()
        write_json(receipt_path, receipt)
        try:
            acknowledgement = request(args.server, "/prompt", {"prompt": graph, "client_id": receipt["client_id"]})
        except urllib.error.HTTPError as error:
            receipt["status"] = "rejected"
            receipt["error"] = error.read().decode()
            write_json(receipt_path, receipt)
            raise RuntimeError(receipt["error"]) from error
        # A network exception leaves status=submitting. Never blindly retry POST.
        if not acknowledgement.get("prompt_id"):
            receipt["status"], receipt["error"] = "rejected", acknowledgement
            write_json(receipt_path, receipt)
            raise ValueError(f"prompt rejected: {acknowledgement}")
        receipt["prompt_id"], receipt["status"] = acknowledgement["prompt_id"], "running"
        write_json(receipt_path, receipt)
        print(f"Submitted {args.graph.name}: {receipt['prompt_id']}", flush=True)
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        history = request(args.server, "/history/" + receipt["prompt_id"])
        if receipt["prompt_id"] in history:
            item = history[receipt["prompt_id"]]
            write_json(args.output / "history.json", item)
            if item["status"]["status_str"] != "success":
                receipt["status"] = "execution_failed"
                write_json(receipt_path, receipt)
                raise RuntimeError(json.dumps(item["status"]))
            outputs = {}
            for node_id, name in (("16", "raw"), ("19", "server-composite")):
                files = item["outputs"][node_id]["images"]
                if len(files) != 1:
                    raise ValueError("expected one image per output node")
                outputs[name] = request(args.server, "/view?" + urllib.parse.urlencode(files[0]), raw=True)
                (args.output / (name + ".png")).write_bytes(outputs[name])
            original = json.loads(graph_bytes)
            blank, _ = asset_path(args.assets, original["17"]["inputs"]["image"])
            mask, _ = asset_path(args.assets, original["8"]["inputs"]["image"])
            composite, server_error = verify_composite(blank.read_bytes(), outputs["raw"], mask.read_bytes(), outputs["server-composite"])
            composite.save(args.output / "composite.png")
            working = composite.resize((256, 256), Image.Resampling.NEAREST)
            working.save(args.output / "working.png")
            working.resize((48, 48), Image.Resampling.NEAREST).save(args.output / "native-48.png")
            receipt.update(status="complete", elapsed_seconds=time.time() - receipt["submitted_at_epoch"],
                           system_after=request(args.server, "/system_stats"), server_composite_max_channel_difference=server_error,
                           output_sha256={name: sha(data) for name, data in outputs.items()}, outside_mask_changed_pixels=0)
            write_json(receipt_path, receipt)
            print(f"Complete in {receipt['elapsed_seconds']:.1f}s; protected pixels unchanged; native preview saved.", flush=True)
            return
        time.sleep(2)
    raise TimeoutError("job still pending; resume this output directory, do not resubmit")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", default="http://127.0.0.1:8189")
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--graph", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=900)
    args = parser.parse_args()
    graphs = args.graph
    for graph in graphs:
        output = args.output / graph.stem if len(graphs) > 1 else args.output
        run(argparse.Namespace(**{**vars(args), "graph": graph, "output": output}))


if __name__ == "__main__":
    main()
