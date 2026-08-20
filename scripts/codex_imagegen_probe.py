#!/usr/bin/env python3
"""Exercise Codex App Server image generation without an OpenAI API key."""

import argparse
import json
import os
import queue
import shutil
import signal
import stat
import struct
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path


MAX_IMAGE_BYTES = 64 * 1024 * 1024
MAX_PROTOCOL_LINE_CHARS = ((MAX_IMAGE_BYTES + 2) // 3) * 4 + 1024 * 1024
MAX_IMAGE_DIMENSION = 4096
DEFAULT_TIMEOUT_SECONDS = 300.0
DEFAULT_PROMPT = (
    "Generate exactly one square 1024x1024 PNG of a single moss-covered "
    "science-fiction boulder prop, isolated on a plain white background, "
    "three-quarter view, no text, and no border. This is a transport "
    "feasibility check; do not inspect or edit files."
)
REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPOSITORY_ROOT / "build" / "codex-imagegen-feasibility.png"


class ProbeError(Exception):
    """The live probe could not establish the required integration contract."""


@dataclass(frozen=True)
class ProbeResult:
    output_path: Path
    width: int
    height: int
    plan_type: str
    user_agent: str
    revised_prompt: str | None
    elapsed_seconds: float


@dataclass(frozen=True)
class _ReaderStopped:
    error: Exception | None


def _child_environment(base_environment=None):
    environment = dict(os.environ if base_environment is None else base_environment)
    environment.pop("OPENAI_API_KEY", None)
    return environment


class AppServerClient:
    """One Codex App Server child connected through its default JSONL protocol."""

    def __init__(
        self,
        codex_binary,
        cwd,
        timeout_seconds=DEFAULT_TIMEOUT_SECONDS,
        trace=False,
        environment=None,
    ):
        self._timeout_seconds = timeout_seconds
        self._trace = trace
        self._next_request_id = 1
        self._messages = queue.Queue()
        self._pending = deque()
        self._stderr_lines = deque(maxlen=100)
        self._closed = False

        try:
            self._process = subprocess.Popen(
                [str(codex_binary), "app-server", "--stdio"],
                cwd=cwd,
                env=_child_environment(environment),
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                bufsize=1,
                start_new_session=(os.name == "posix"),
            )
        except OSError as error:
            raise ProbeError(f"Could not start Codex App Server: {error}") from error

        self._stdout_thread = threading.Thread(
            target=self._read_stdout, name="codex-app-server-stdout", daemon=True
        )
        self._stderr_thread = threading.Thread(
            target=self._read_stderr, name="codex-app-server-stderr", daemon=True
        )
        self._stdout_thread.start()
        self._stderr_thread.start()

    def __enter__(self):
        return self

    def __exit__(self, _error_type, _error, _traceback):
        self.close()

    def _read_stdout(self):
        error = None
        try:
            assert self._process.stdout is not None
            for line in self._process.stdout:
                if len(line) > MAX_PROTOCOL_LINE_CHARS:
                    raise ProbeError("Codex App Server emitted an oversized protocol line")
                try:
                    message = json.loads(line)
                except json.JSONDecodeError as parse_error:
                    raise ProbeError(
                        f"Codex App Server emitted invalid JSON: {parse_error}"
                    ) from parse_error
                if not isinstance(message, dict):
                    raise ProbeError("Codex App Server emitted a non-object JSON message")
                self._messages.put(message)
        except Exception as read_error:  # Thread boundary: report through the queue.
            error = read_error
        finally:
            self._messages.put(_ReaderStopped(error))

    def _read_stderr(self):
        assert self._process.stderr is not None
        for line in self._process.stderr:
            self._stderr_lines.append(line.rstrip())

    def _diagnostics(self):
        if not self._stderr_lines:
            return ""
        return "\nCodex diagnostics:\n" + "\n".join(list(self._stderr_lines)[-10:])

    def _send(self, message):
        if self._closed:
            raise ProbeError("Codex App Server client is closed")
        assert self._process.stdin is not None
        try:
            self._process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
            self._process.stdin.flush()
        except (BrokenPipeError, OSError) as error:
            raise ProbeError(
                f"Could not write to Codex App Server: {error}{self._diagnostics()}"
            ) from error

    def notify(self, method, params):
        self._send({"method": method, "params": params})

    def request(self, method, params, timeout_seconds=None):
        request_id = self._next_request_id
        self._next_request_id += 1
        self._send({"method": method, "id": request_id, "params": params})

        deadline = time.monotonic() + (timeout_seconds or self._timeout_seconds)
        while True:
            message = self._read_message(deadline)
            if "method" in message and "id" in message:
                raise ProbeError(
                    f"Unexpected server request {message['method']!r}; the probe does not "
                    "grant approvals or host dynamic tools"
                )
            if message.get("id") != request_id:
                self._pending.append(message)
                continue
            if "error" in message:
                error = message["error"]
                detail = error.get("message", str(error)) if isinstance(error, dict) else str(error)
                raise ProbeError(f"Codex request {method!r} failed: {detail}")
            if "result" not in message:
                raise ProbeError(f"Codex response to {method!r} has neither result nor error")
            return message["result"]

    def next_message(self, timeout_seconds=None):
        if self._pending:
            message = self._pending.popleft()
        else:
            deadline = time.monotonic() + (timeout_seconds or self._timeout_seconds)
            message = self._read_message(deadline)
        if "method" in message and "id" in message:
            raise ProbeError(f"Unexpected server request {message['method']!r}")
        if self._trace and "method" in message:
            item = message.get("params", {}).get("item", {})
            item_type = f" ({item.get('type')})" if isinstance(item, dict) else ""
            print(f"Codex event: {message['method']}{item_type}", file=sys.stderr)
        return message

    def _read_message(self, deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ProbeError("Timed out waiting for Codex App Server")
        try:
            message = self._messages.get(timeout=remaining)
        except queue.Empty as error:
            raise ProbeError("Timed out waiting for Codex App Server") from error
        if isinstance(message, _ReaderStopped):
            if message.error is not None:
                raise ProbeError(
                    f"Codex protocol reader failed: {message.error}"
                ) from message.error
            status = self._process.poll()
            raise ProbeError(
                f"Codex App Server closed stdout with status {status}{self._diagnostics()}"
            )
        return message

    def close(self):
        if self._closed:
            return
        self._closed = True
        if self._process.stdin is not None:
            try:
                self._process.stdin.close()
            except OSError:
                pass
        try:
            self._process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                if os.name == "posix":
                    os.killpg(self._process.pid, signal.SIGTERM)
                else:
                    self._process.terminate()
                self._process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                try:
                    if os.name == "posix":
                        os.killpg(self._process.pid, signal.SIGKILL)
                    else:
                        self._process.kill()
                    self._process.wait(timeout=5)
                except (OSError, subprocess.TimeoutExpired):
                    pass

        self._stdout_thread.join(timeout=1)
        self._stderr_thread.join(timeout=1)
        for stream in (self._process.stdout, self._process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass


def _required_object(value, context):
    if not isinstance(value, dict):
        raise ProbeError(f"{context} is not an object")
    return value


def _require_chatgpt_account(result):
    account = _required_object(result, "account/read result").get("account")
    account = _required_object(account, "Codex account")
    account_type = account.get("type")
    if account_type != "chatgpt":
        raise ProbeError(
            "Codex is not using ChatGPT authentication. Refusing to generate because "
            f"the active account type is {account_type!r}; run 'codex login' and choose "
            "ChatGPT before retrying."
        )
    return str(account.get("planType") or "unknown")


def _find_imagegen_skill(result, cwd):
    entries = _required_object(result, "skills/list result").get("data")
    if not isinstance(entries, list):
        raise ProbeError("skills/list result has no data array")
    for entry in entries:
        if not isinstance(entry, dict) or entry.get("cwd") != str(cwd):
            continue
        for skill in entry.get("skills", []):
            if not isinstance(skill, dict) or skill.get("name") != "imagegen":
                continue
            if not skill.get("enabled", False):
                raise ProbeError("The Codex imagegen skill is installed but disabled")
            path = skill.get("path")
            if not isinstance(path, str) or not Path(path).is_absolute():
                raise ProbeError("The Codex imagegen skill has no absolute SKILL.md path")
            return path
    raise ProbeError("Codex did not report an enabled imagegen skill")


def _describe_image_failure(item):
    failure = item.get("failure")
    if isinstance(failure, dict) and failure.get("type") == "usageLimitExceeded":
        reset = failure.get("resetsAt")
        suffix = f"; reset timestamp {reset}" if reset is not None else ""
        return f"Codex image-generation usage limit was reached{suffix}"
    result = item.get("result")
    detail = result[:200] if isinstance(result, str) else "no result detail"
    return f"Codex image generation failed with status {item.get('status')!r}: {detail}"


def _wait_for_generation(client, thread_id, turn_id, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    image_items = []
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ProbeError("Timed out waiting for Codex image generation")
        message = client.next_message(remaining)
        method = message.get("method")
        params = message.get("params", {})
        if not isinstance(params, dict):
            continue
        if method == "item/completed":
            if params.get("threadId") != thread_id or params.get("turnId") != turn_id:
                continue
            item = params.get("item")
            if isinstance(item, dict) and item.get("type") == "imageGeneration":
                if item.get("failure") is not None or item.get("status") != "completed":
                    raise ProbeError(_describe_image_failure(item))
                image_items.append(item)
        if method != "turn/completed":
            continue
        turn = params.get("turn")
        if not isinstance(turn, dict) or turn.get("id") != turn_id:
            continue
        if turn.get("status") != "completed":
            raise ProbeError(f"Codex turn ended with status {turn.get('status')!r}")
        if len(image_items) != 1:
            raise ProbeError(
                f"Expected exactly one completed image-generation item, got {len(image_items)}"
            )
        return image_items[0]


def _validate_png(path):
    path = Path(path)
    if not path.is_absolute():
        raise ProbeError(f"Codex returned a non-absolute image path: {path}")
    try:
        metadata = path.lstat()
    except OSError as error:
        raise ProbeError(f"Could not inspect generated image {path}: {error}") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise ProbeError(f"Codex generated-image path is not a regular non-symlink file: {path}")
    if metadata.st_size <= 0 or metadata.st_size > MAX_IMAGE_BYTES:
        raise ProbeError(
            f"Codex generated image has invalid size {metadata.st_size} bytes (maximum "
            f"{MAX_IMAGE_BYTES})"
        )
    try:
        with path.open("rb") as image_file:
            header = image_file.read(24)
    except OSError as error:
        raise ProbeError(f"Could not read generated image {path}: {error}") from error
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ProbeError("Codex generated image is not a PNG with an IHDR header")
    width, height = struct.unpack(">II", header[16:24])
    if not (0 < width <= MAX_IMAGE_DIMENSION and 0 < height <= MAX_IMAGE_DIMENSION):
        raise ProbeError(f"Codex generated image dimensions are invalid: {width}x{height}")
    return width, height


def _copy_result(source_path, output_path, overwrite):
    source_path = Path(source_path)
    output_path = Path(output_path).resolve()
    if output_path.exists() and not overwrite:
        raise ProbeError(f"Output already exists: {output_path}; pass --overwrite to replace it")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        if source_path.resolve() != output_path:
            shutil.copyfile(source_path, output_path)
    except OSError as error:
        raise ProbeError(f"Could not retain generated image at {output_path}: {error}") from error
    return output_path


def run_probe(client, staging_dir, prompt, output_path, timeout_seconds, overwrite=False):
    started = time.monotonic()
    initialize = client.request(
        "initialize",
        {
            "clientInfo": {
                "name": "zebes_imagegen_probe",
                "title": "Zebes Image Generation Probe",
                "version": "0.1.0",
            }
        },
    )
    client.notify("initialized", {})
    user_agent = str(
        _required_object(initialize, "initialize result").get("userAgent")
        or "unknown"
    )

    account = client.request("account/read", {"refreshToken": False})
    plan_type = _require_chatgpt_account(account)
    skills = client.request(
        "skills/list", {"cwds": [str(staging_dir)], "forceReload": False}
    )
    skill_path = _find_imagegen_skill(skills, staging_dir)

    thread_result = client.request(
        "thread/start",
        {
            "cwd": str(staging_dir),
            "ephemeral": True,
            "approvalPolicy": "never",
            "sandbox": "workspace-write",
            "developerInstructions": (
                "Act only as an image-generation worker. Use the explicitly supplied "
                "imagegen skill, do not inspect unrelated files, do not run shell commands, "
                "generate exactly one image, and then stop."
            ),
        },
    )
    thread = _required_object(
        _required_object(thread_result, "thread/start result").get("thread"), "thread"
    )
    thread_id = thread.get("id")
    if not isinstance(thread_id, str) or not thread_id:
        raise ProbeError("thread/start returned no thread id")

    turn_result = client.request(
        "turn/start",
        {
            "threadId": thread_id,
            "input": [
                {"type": "text", "text": f"$imagegen {prompt}"},
                {"type": "skill", "name": "imagegen", "path": skill_path},
            ],
        },
    )
    turn = _required_object(
        _required_object(turn_result, "turn/start result").get("turn"), "turn"
    )
    turn_id = turn.get("id")
    if not isinstance(turn_id, str) or not turn_id:
        raise ProbeError("turn/start returned no turn id")

    try:
        image_item = _wait_for_generation(client, thread_id, turn_id, timeout_seconds)
    except Exception:
        try:
            client.request(
                "turn/interrupt",
                {"threadId": thread_id, "turnId": turn_id},
                timeout_seconds=5,
            )
        except Exception:
            pass
        raise

    saved_path = image_item.get("savedPath")
    if not isinstance(saved_path, str) or not saved_path:
        raise ProbeError("Completed image-generation item did not include savedPath")
    width, height = _validate_png(saved_path)
    retained_path = _copy_result(saved_path, output_path, overwrite)
    return ProbeResult(
        output_path=retained_path,
        width=width,
        height=height,
        plan_type=plan_type,
        user_agent=user_agent,
        revised_prompt=image_item.get("revisedPrompt"),
        elapsed_seconds=time.monotonic() - started,
    )


def _resolve_codex_binary(value):
    if os.sep in value or (os.altsep is not None and os.altsep in value):
        path = Path(value).expanduser()
        if not path.is_file():
            raise ProbeError(f"Codex binary does not exist: {path}")
        return path.resolve()
    resolved = shutil.which(value)
    if resolved is None:
        raise ProbeError(f"Could not find {value!r} on PATH")
    return Path(resolved).resolve()


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--accept-codex-usage",
        action="store_true",
        help="confirm that this invocation may consume one subscription-backed image generation",
    )
    parser.add_argument("--codex-bin", default="codex")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--prompt", default=DEFAULT_PROMPT)
    parser.add_argument("--timeout-seconds", type=float, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--trace", action="store_true")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if not args.accept_codex_usage:
        print(
            "codex_imagegen_probe.py: refusing to generate without --accept-codex-usage",
            file=sys.stderr,
        )
        return 2
    if args.timeout_seconds <= 0:
        print("codex_imagegen_probe.py: --timeout-seconds must be positive", file=sys.stderr)
        return 2

    try:
        codex_binary = _resolve_codex_binary(args.codex_bin)
        with tempfile.TemporaryDirectory(prefix="zebes-codex-imagegen-") as temporary:
            staging_dir = Path(temporary).resolve()
            with AppServerClient(
                codex_binary,
                staging_dir,
                timeout_seconds=args.timeout_seconds,
                trace=args.trace,
            ) as client:
                result = run_probe(
                    client,
                    staging_dir,
                    args.prompt,
                    args.output,
                    args.timeout_seconds,
                    overwrite=args.overwrite,
                )
    except ProbeError as error:
        print(f"codex_imagegen_probe.py: {error}", file=sys.stderr)
        return 1

    print(f"Generated {result.width}x{result.height} PNG at {result.output_path}")
    print(f"Authentication: ChatGPT ({result.plan_type})")
    print(f"Codex: {result.user_agent}")
    print(f"Elapsed: {result.elapsed_seconds:.1f}s")
    if result.revised_prompt:
        print(f"Revised prompt: {result.revised_prompt}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
