import contextlib
import importlib.util
import io
import json
import os
import stat
import struct
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "codex_imagegen_probe.py"
SPEC = importlib.util.spec_from_file_location("codex_imagegen_probe", SCRIPT_PATH)
codex_imagegen_probe = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = codex_imagegen_probe
SPEC.loader.exec_module(codex_imagegen_probe)


def write_probe_png(path, width=1024, height=1024):
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + struct.pack(">I", 13)
        + b"IHDR"
        + struct.pack(">II", width, height)
    )


class FakeClient:
    def __init__(self, responses):
        self.responses = responses
        self.requests = []
        self.notifications = []

    def request(self, method, params, timeout_seconds=None):
        self.requests.append((method, params, timeout_seconds))
        return self.responses[method]

    def notify(self, method, params):
        self.notifications.append((method, params))


class CodexImagegenProbeTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.temp_dir = Path(self._temp.name).resolve()
        self.addCleanup(self._temp.cleanup)

    def make_fake_app_server(self, image_path, transcript_path, environment_path):
        server_path = self.temp_dir / "fake-codex"
        server_path.write_text(
            f"""#!{sys.executable}
import json
import os
import sys

image_path = {str(image_path)!r}
transcript_path = {str(transcript_path)!r}
environment_path = {str(environment_path)!r}
with open(environment_path, "w", encoding="utf-8") as environment_file:
    environment_file.write("present" if "OPENAI_API_KEY" in os.environ else "absent")

def send(value):
    print(json.dumps(value, separators=(",", ":")), flush=True)

for line in sys.stdin:
    message = json.loads(line)
    with open(transcript_path, "a", encoding="utf-8") as transcript:
        transcript.write(json.dumps(message) + "\\n")
    method = message.get("method")
    request_id = message.get("id")
    if request_id is None:
        continue
    if method == "initialize":
        send({{"id": request_id, "result": {{"userAgent": "fake-codex/1.0"}}}})
    elif method == "account/read":
        send({{
            "id": request_id,
            "result": {{
                "account": {{
                    "type": "chatgpt",
                    "planType": "pro",
                    "email": "do-not-log@example.com",
                }}
            }},
        }})
    elif method == "skills/list":
        cwd = message["params"]["cwds"][0]
        send({{
            "id": request_id,
            "result": {{
                "data": [{{
                    "cwd": cwd,
                    "skills": [{{
                        "name": "imagegen",
                        "path": "/fake/imagegen/SKILL.md",
                        "enabled": True,
                    }}],
                }}]
            }},
        }})
    elif method == "thread/start":
        send({{"id": request_id, "result": {{"thread": {{"id": "thread-1"}}}}}})
    elif method == "turn/start":
        send({{"id": request_id, "result": {{"turn": {{"id": "turn-1"}}}}}})
        send({{
            "method": "item/completed",
            "params": {{
                "threadId": "thread-1",
                "turnId": "turn-1",
                "item": {{
                    "type": "imageGeneration",
                    "id": "image-1",
                    "status": "completed",
                    "savedPath": image_path,
                    "revisedPrompt": "revised test prompt",
                    "failure": None,
                }},
            }},
        }})
        send({{
            "method": "turn/completed",
            "params": {{
                "threadId": "thread-1",
                "turn": {{"id": "turn-1", "status": "completed"}},
            }},
        }})
    elif method == "turn/interrupt":
        send({{"id": request_id, "result": {{}}}})
    else:
        send({{"id": request_id, "error": {{"message": "unexpected method " + str(method)}}}})
""",
            encoding="utf-8",
        )
        server_path.chmod(server_path.stat().st_mode | stat.S_IXUSR)
        return server_path

    def test_fake_app_server_exercises_subscription_only_contract(self):
        generated_path = self.temp_dir / "generated.png"
        output_path = self.temp_dir / "retained.png"
        transcript_path = self.temp_dir / "transcript.jsonl"
        environment_path = self.temp_dir / "environment.txt"
        staging_dir = self.temp_dir / "staging"
        staging_dir.mkdir()
        write_probe_png(generated_path)
        server_path = self.make_fake_app_server(
            generated_path, transcript_path, environment_path
        )

        environment = dict(os.environ)
        environment["OPENAI_API_KEY"] = "must-not-reach-child"
        with codex_imagegen_probe.AppServerClient(
            server_path,
            staging_dir,
            timeout_seconds=5,
            environment=environment,
        ) as client:
            result = codex_imagegen_probe.run_probe(
                client,
                staging_dir,
                "test prompt",
                output_path,
                timeout_seconds=5,
            )

        self.assertEqual(environment_path.read_text(encoding="utf-8"), "absent")
        self.assertEqual(result.output_path, output_path)
        self.assertEqual((result.width, result.height), (1024, 1024))
        self.assertEqual(result.plan_type, "pro")
        self.assertEqual(result.user_agent, "fake-codex/1.0")
        self.assertEqual(result.revised_prompt, "revised test prompt")
        self.assertEqual(output_path.read_bytes(), generated_path.read_bytes())

        messages = [
            json.loads(line)
            for line in transcript_path.read_text(encoding="utf-8").splitlines()
        ]
        methods = [message["method"] for message in messages]
        self.assertEqual(
            methods,
            [
                "initialize",
                "initialized",
                "account/read",
                "skills/list",
                "thread/start",
                "turn/start",
            ],
        )
        thread_params = next(
            message["params"]
            for message in messages
            if message["method"] == "thread/start"
        )
        self.assertTrue(thread_params["ephemeral"])
        self.assertEqual(thread_params["approvalPolicy"], "never")
        self.assertEqual(thread_params["sandbox"], "workspace-write")
        turn_input = next(
            message["params"]["input"]
            for message in messages
            if message["method"] == "turn/start"
        )
        self.assertEqual(turn_input[0], {"type": "text", "text": "$imagegen test prompt"})
        self.assertEqual(
            turn_input[1],
            {
                "type": "skill",
                "name": "imagegen",
                "path": "/fake/imagegen/SKILL.md",
            },
        )

    def test_refuses_non_chatgpt_authentication_before_starting_thread(self):
        client = FakeClient(
            {
                "initialize": {"userAgent": "fake"},
                "account/read": {"account": {"type": "apiKey"}},
            }
        )

        with self.assertRaisesRegex(
            codex_imagegen_probe.ProbeError, "not using ChatGPT authentication"
        ):
            codex_imagegen_probe.run_probe(
                client,
                self.temp_dir,
                "test prompt",
                self.temp_dir / "output.png",
                timeout_seconds=5,
            )

        self.assertEqual(
            [method for method, _params, _timeout in client.requests],
            ["initialize", "account/read"],
        )

    def test_requires_explicit_usage_confirmation(self):
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            result = codex_imagegen_probe.main([])

        self.assertEqual(result, 2)
        self.assertIn("--accept-codex-usage", stderr.getvalue())

    def test_rejects_symlinked_generated_image(self):
        generated_path = self.temp_dir / "generated.png"
        symlink_path = self.temp_dir / "generated-link.png"
        write_probe_png(generated_path)
        symlink_path.symlink_to(generated_path)

        with self.assertRaisesRegex(
            codex_imagegen_probe.ProbeError, "regular non-symlink"
        ):
            codex_imagegen_probe._validate_png(symlink_path)


if __name__ == "__main__":
    unittest.main()
