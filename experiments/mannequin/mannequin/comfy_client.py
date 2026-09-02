"""HTTP bridge to a ComfyUI instance.

ComfyUI runs on the RTX 3090 box and this talks to it over the LAN. That is our
own machine, not a third-party provider: no account, no per-image cost, and no
character art leaving the house.

Standard library only, like the rest of this experiment — `urllib` covers every
route used here, including the multipart upload.

There are no retries anywhere. A run that transparently retried would hide the
one thing this experiment is trying to measure, which is whether a given set of
conditioning maps reliably produces the same character. Waiting on a queued
prompt is not a retry; a failed prompt stops the run.
"""

from __future__ import annotations

import json
import mimetypes
import os
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from pathlib import Path

# The 3090 box. Override per machine rather than editing this.
DEFAULT_BASE_URL = "http://127.0.0.1:8188"
BASE_URL_ENV = "ZEBES_COMFY_URL"

DEFAULT_TIMEOUT_SECONDS = 30.0
DEFAULT_GENERATION_TIMEOUT_SECONDS = 600.0
DEFAULT_POLL_SECONDS = 1.0


class ComfyError(RuntimeError):
    """Raised for any unusable response from ComfyUI."""


def base_url_from_environment() -> str:
    return os.environ.get(BASE_URL_ENV, DEFAULT_BASE_URL)


@dataclass(frozen=True)
class UploadedImage:
    name: str
    subfolder: str
    type: str

    @property
    def reference(self) -> str:
        """The string a LoadImage node expects for this file."""
        return f"{self.subfolder}/{self.name}" if self.subfolder else self.name


@dataclass(frozen=True)
class OutputImage:
    node_id: str
    filename: str
    subfolder: str
    type: str


def _encode_multipart(
    fields: dict[str, str], file_field: str, path: Path
) -> tuple[bytes, str]:
    boundary = f"----zebes{uuid.uuid4().hex}"
    line_break = b"\r\n"
    body = bytearray()

    for name, value in fields.items():
        body += f"--{boundary}".encode() + line_break
        body += (
            f'Content-Disposition: form-data; name="{name}"'.encode() + line_break
        )
        body += line_break + value.encode() + line_break

    content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
    body += f"--{boundary}".encode() + line_break
    body += (
        f'Content-Disposition: form-data; name="{file_field}"; '
        f'filename="{path.name}"'.encode()
        + line_break
    )
    body += f"Content-Type: {content_type}".encode() + line_break
    body += line_break + path.read_bytes() + line_break
    body += f"--{boundary}--".encode() + line_break

    return bytes(body), f"multipart/form-data; boundary={boundary}"


class ComfyClient:
    """One conversation with one ComfyUI server.

    `client_id` is stable for the life of the client so a caller could attach a
    websocket for progress against the same identity; this class itself only
    polls, because a run here is a handful of prompts rather than a live UI.
    """

    def __init__(
        self,
        base_url: str | None = None,
        timeout: float = DEFAULT_TIMEOUT_SECONDS,
        client_id: str | None = None,
    ) -> None:
        self.base_url = (base_url or base_url_from_environment()).rstrip("/")
        self.timeout = timeout
        self.client_id = client_id or f"zebes-mannequin-{uuid.uuid4().hex[:12]}"

    def _open(self, request: urllib.request.Request, timeout: float | None = None):
        try:
            return urllib.request.urlopen(request, timeout=timeout or self.timeout)
        except urllib.error.HTTPError as error:
            # HTTPError is itself a file object over a spooled temp file, so it
            # leaks a handle unless it is closed after the body is read.
            with error:
                detail = error.read().decode("utf-8", "replace").strip()
            raise ComfyError(
                f"{request.get_method()} {request.full_url} failed with "
                f"HTTP {error.code}: {detail or error.reason}"
            ) from error
        except urllib.error.URLError as error:
            raise ComfyError(
                f"cannot reach ComfyUI at {self.base_url} ({error.reason}). "
                f"Set {BASE_URL_ENV} to the 3090 box, and start ComfyUI with "
                "--listen so it accepts connections from the network."
            ) from error

    def _get_json(self, path: str) -> dict:
        request = urllib.request.Request(f"{self.base_url}{path}", method="GET")
        with self._open(request) as response:
            return json.loads(response.read().decode("utf-8"))

    def system_stats(self) -> dict:
        """Reachability check. Call before a run rather than mid-batch."""
        return self._get_json("/system_stats")

    def upload_image(self, path: Path, subfolder: str = "") -> UploadedImage:
        if not path.is_file():
            raise ComfyError(f"no such file to upload: {path}")

        fields = {"type": "input", "overwrite": "true"}
        if subfolder:
            fields["subfolder"] = subfolder
        body, content_type = _encode_multipart(fields, "image", path)

        request = urllib.request.Request(
            f"{self.base_url}/upload/image",
            data=body,
            headers={"Content-Type": content_type},
            method="POST",
        )
        with self._open(request) as response:
            payload = json.loads(response.read().decode("utf-8"))

        if "name" not in payload:
            raise ComfyError(f"upload of {path.name} returned no name: {payload}")
        return UploadedImage(
            name=payload["name"],
            subfolder=payload.get("subfolder", ""),
            type=payload.get("type", "input"),
        )

    def submit(self, workflow: dict) -> str:
        body = json.dumps(
            {"prompt": workflow, "client_id": self.client_id}
        ).encode("utf-8")
        request = urllib.request.Request(
            f"{self.base_url}/prompt",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with self._open(request) as response:
            payload = json.loads(response.read().decode("utf-8"))

        # A graph ComfyUI rejects comes back as 200 with node_errors on some
        # builds and as 400 on others, so check the body as well as the status.
        if payload.get("node_errors"):
            raise ComfyError(
                f"ComfyUI rejected the workflow: {json.dumps(payload['node_errors'])}"
            )
        if "prompt_id" not in payload:
            raise ComfyError(f"queueing returned no prompt_id: {payload}")
        return str(payload["prompt_id"])

    def history(self, prompt_id: str) -> dict | None:
        payload = self._get_json(f"/history/{urllib.parse.quote(prompt_id)}")
        return payload.get(prompt_id)

    def wait_for(
        self,
        prompt_id: str,
        timeout_seconds: float = DEFAULT_GENERATION_TIMEOUT_SECONDS,
        poll_seconds: float = DEFAULT_POLL_SECONDS,
        sleep=time.sleep,
        now=time.monotonic,
    ) -> dict:
        """Block until the prompt finishes, then return its history entry."""
        deadline = now() + timeout_seconds
        while True:
            entry = self.history(prompt_id)
            if entry is not None:
                status = entry.get("status", {})
                if status.get("status_str") == "error" or (
                    status.get("completed") is False and "error" in status
                ):
                    raise ComfyError(
                        f"prompt {prompt_id} failed: "
                        f"{json.dumps(status.get('messages', status))}"
                    )
                if entry.get("outputs"):
                    return entry

            if now() >= deadline:
                raise ComfyError(
                    f"prompt {prompt_id} produced no output within "
                    f"{timeout_seconds:g}s"
                )
            sleep(poll_seconds)

    def outputs_of(self, entry: dict) -> tuple[OutputImage, ...]:
        images: list[OutputImage] = []
        for node_id, output in entry.get("outputs", {}).items():
            for image in output.get("images", []):
                images.append(
                    OutputImage(
                        node_id=str(node_id),
                        filename=image["filename"],
                        subfolder=image.get("subfolder", ""),
                        type=image.get("type", "output"),
                    )
                )
        if not images:
            raise ComfyError("the prompt completed but produced no images")
        return tuple(images)

    def download(self, image: OutputImage) -> bytes:
        query = urllib.parse.urlencode(
            {
                "filename": image.filename,
                "subfolder": image.subfolder,
                "type": image.type,
            }
        )
        request = urllib.request.Request(f"{self.base_url}/view?{query}", method="GET")
        with self._open(request) as response:
            return response.read()

    def run(
        self,
        workflow: dict,
        out_dir: Path,
        timeout_seconds: float = DEFAULT_GENERATION_TIMEOUT_SECONDS,
    ) -> tuple[Path, ...]:
        """Queue a workflow, wait for it, and write every image it produced."""
        entry = self.wait_for(self.submit(workflow), timeout_seconds=timeout_seconds)
        out_dir.mkdir(parents=True, exist_ok=True)

        written: list[Path] = []
        for image in self.outputs_of(entry):
            destination = out_dir / image.filename
            destination.write_bytes(self.download(image))
            written.append(destination)
        return tuple(written)
