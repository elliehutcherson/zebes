"""Tests for the ComfyUI bridge and workflow template patching.

Everything here runs against a stub HTTP server in-process, so the suite passes
with the 3090 box switched off. That matters: a test that needs the GPU machine
awake is a test that gets skipped, and a skipped test is not a gate.
"""

import json
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

EXPERIMENT_ROOT = Path(__file__).parent.parent / "experiments" / "mannequin"
if str(EXPERIMENT_ROOT) not in sys.path:
    sys.path.insert(0, str(EXPERIMENT_ROOT))

from mannequin import workflow  # noqa: E402
from mannequin.comfy_client import ComfyClient, ComfyError, OutputImage  # noqa: E402

PNG_BYTES = b"\x89PNG\r\n\x1a\nstub-image-payload"


class StubState:
    """What the stub server should do, and what it recorded."""

    def __init__(self):
        self.upload_response = {"name": "guide.png", "subfolder": "", "type": "input"}
        self.prompt_response = {"prompt_id": "abc123", "number": 1}
        self.prompt_status = 200
        # Successive replies to /history/{id}; the last one repeats.
        self.history_replies = [{}]
        self.history_calls = 0
        self.requests = []


class StubHandler(BaseHTTPRequestHandler):
    state: StubState

    def log_message(self, *args):
        pass

    def _send(self, payload, status=200, raw=None):
        body = raw if raw is not None else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.state.requests.append(("GET", self.path))
        if self.path == "/system_stats":
            self._send({"system": {"comfyui_version": "stub"}})
            return
        if self.path.startswith("/history/"):
            index = min(self.state.history_calls, len(self.state.history_replies) - 1)
            self.state.history_calls += 1
            self._send(self.state.history_replies[index])
            return
        if self.path.startswith("/view"):
            self._send(None, raw=PNG_BYTES)
            return
        self._send({"error": "not found"}, status=404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        self.state.requests.append(("POST", self.path, body, dict(self.headers)))

        if self.path == "/upload/image":
            self._send(self.state.upload_response)
            return
        if self.path == "/prompt":
            self._send(self.state.prompt_response, status=self.state.prompt_status)
            return
        self._send({"error": "not found"}, status=404)


class ComfyClientTest(unittest.TestCase):
    def setUp(self):
        self.state = StubState()
        handler = type("BoundHandler", (StubHandler,), {"state": self.state})
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        # A short poll interval keeps shutdown() from costing the socketserver
        # default of half a second on every one of these tests.
        threading.Thread(
            target=self.server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True
        ).start()
        # Cleanups run last-registered first, so this closes the listening
        # socket only after serve_forever has stopped.
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        host, port = self.server.server_address
        self.client = ComfyClient(f"http://{host}:{port}", timeout=5.0)

    def completed_history(self):
        return {
            "abc123": {
                "status": {"status_str": "success", "completed": True},
                "outputs": {
                    "9": {
                        "images": [
                            {
                                "filename": "zebes_00001_.png",
                                "subfolder": "",
                                "type": "output",
                            }
                        ]
                    }
                },
            }
        }

    def test_system_stats_reaches_the_server(self):
        self.assertIn("system", self.client.system_stats())

    def test_an_unreachable_host_says_how_to_fix_it(self):
        # Port 1 is reserved and never listening.
        offline = ComfyClient("http://127.0.0.1:1", timeout=2.0)

        with self.assertRaises(ComfyError) as caught:
            offline.system_stats()

        message = str(caught.exception)
        self.assertIn("ZEBES_COMFY_URL", message)
        self.assertIn("--listen", message)

    def test_upload_posts_multipart_and_returns_a_load_image_reference(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "depth.png"
            path.write_bytes(PNG_BYTES)

            uploaded = self.client.upload_image(path)

        self.assertEqual(uploaded.reference, "guide.png")
        method, route, body, headers = self.state.requests[-1]
        self.assertEqual((method, route), ("POST", "/upload/image"))
        self.assertIn("multipart/form-data; boundary=", headers["Content-Type"])
        self.assertIn(b'name="image"; filename="depth.png"', body)
        self.assertIn(PNG_BYTES, body)

    def test_upload_reference_includes_a_subfolder_when_present(self):
        import tempfile

        self.state.upload_response = {
            "name": "guide.png",
            "subfolder": "mannequin",
            "type": "input",
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "depth.png"
            path.write_bytes(PNG_BYTES)

            uploaded = self.client.upload_image(path, subfolder="mannequin")

        self.assertEqual(uploaded.reference, "mannequin/guide.png")

    def test_uploading_a_missing_file_is_refused_before_any_request(self):
        before = len(self.state.requests)

        with self.assertRaises(ComfyError):
            self.client.upload_image(Path("absent.png"))

        self.assertEqual(len(self.state.requests), before)

    def test_submit_sends_the_workflow_and_returns_a_prompt_id(self):
        prompt_id = self.client.submit({"1": {"class_type": "X", "inputs": {}}})

        self.assertEqual(prompt_id, "abc123")
        _, route, body, _ = self.state.requests[-1]
        self.assertEqual(route, "/prompt")
        payload = json.loads(body)
        self.assertEqual(payload["prompt"]["1"]["class_type"], "X")
        self.assertTrue(payload["client_id"].startswith("zebes-mannequin-"))

    def test_a_rejected_graph_reports_the_node_errors(self):
        self.state.prompt_response = {
            "error": "invalid prompt",
            "node_errors": {"4": {"errors": [{"message": "value not in list"}]}},
        }

        with self.assertRaises(ComfyError) as caught:
            self.client.submit({})

        self.assertIn("value not in list", str(caught.exception))

    def test_an_http_error_body_is_included_in_the_message(self):
        self.state.prompt_status = 400
        self.state.prompt_response = {"error": "prompt has no outputs"}

        with self.assertRaises(ComfyError) as caught:
            self.client.submit({})

        self.assertIn("prompt has no outputs", str(caught.exception))

    def test_wait_polls_until_outputs_appear(self):
        self.state.history_replies = [{}, {}, self.completed_history()]
        slept = []

        entry = self.client.wait_for(
            "abc123", timeout_seconds=30.0, sleep=slept.append, now=lambda: 0.0
        )

        self.assertIn("outputs", entry)
        self.assertEqual(len(slept), 2)

    def test_wait_reports_a_failed_prompt_rather_than_waiting_it_out(self):
        self.state.history_replies = [
            {
                "abc123": {
                    "status": {
                        "status_str": "error",
                        "completed": False,
                        "messages": [["execution_error", {"exception_message": "OOM"}]],
                    }
                }
            }
        ]

        with self.assertRaises(ComfyError) as caught:
            self.client.wait_for("abc123", sleep=lambda _: None, now=lambda: 0.0)

        self.assertIn("OOM", str(caught.exception))

    def test_wait_times_out_with_the_budget_in_the_message(self):
        clock = iter([0.0, 0.0, 99.0, 99.0])

        with self.assertRaises(ComfyError) as caught:
            self.client.wait_for(
                "abc123",
                timeout_seconds=10.0,
                sleep=lambda _: None,
                now=lambda: next(clock),
            )

        self.assertIn("10s", str(caught.exception))

    def test_a_completed_prompt_with_no_images_is_an_error(self):
        with self.assertRaises(ComfyError):
            self.client.outputs_of({"outputs": {"9": {"images": []}}})

    def test_download_passes_the_view_query_parameters(self):
        data = self.client.download(
            OutputImage(node_id="9", filename="a b.png", subfolder="sub", type="output")
        )

        self.assertEqual(data, PNG_BYTES)
        _, route = self.state.requests[-1]
        self.assertIn("filename=a+b.png", route)
        self.assertIn("subfolder=sub", route)
        self.assertIn("type=output", route)

    def test_run_writes_every_output_image(self):
        import tempfile

        self.state.history_replies = [self.completed_history()]

        with tempfile.TemporaryDirectory() as directory:
            written = self.client.run(
                {"1": {"class_type": "X", "inputs": {}}}, Path(directory) / "out"
            )

            self.assertEqual(len(written), 1)
            self.assertEqual(written[0].name, "zebes_00001_.png")
            self.assertEqual(written[0].read_bytes(), PNG_BYTES)


class WorkflowTemplateTest(unittest.TestCase):
    def template(self):
        return {
            "3": {
                "class_type": "KSampler",
                "inputs": {"seed": 0, "steps": 20, "cfg": 7.0},
                "_meta": {"title": workflow.SEED},
            },
            "6": {
                "class_type": "CLIPTextEncode",
                "inputs": {"text": "placeholder", "clip": ["4", 1]},
                "_meta": {"title": workflow.POSITIVE_PROMPT},
            },
            "10": {
                "class_type": "LoadImage",
                "inputs": {"image": "example.png", "upload": "image"},
                "_meta": {"title": workflow.CONTROL_IMAGE},
            },
            "4": {
                "class_type": "CheckpointLoaderSimple",
                "inputs": {"ckpt_name": "sdxl.safetensors"},
                "_meta": {"title": "Load Checkpoint"},
            },
        }

    def write_template(self, directory, data):
        path = Path(directory) / "workflow.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path

    def test_a_ui_format_export_is_rejected_with_the_fix(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            path = self.write_template(directory, {"nodes": [], "links": []})

            with self.assertRaises(workflow.WorkflowError) as caught:
                workflow.load(path)

        self.assertIn("Save (API Format)", str(caught.exception))

    def test_a_node_without_class_type_is_rejected(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            path = self.write_template(directory, {"3": {"inputs": {}}})

            with self.assertRaises(workflow.WorkflowError):
                workflow.load(path)

    def test_an_api_format_export_loads(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            path = self.write_template(directory, self.template())

            self.assertEqual(len(workflow.load(path)), 4)

    def test_handles_list_only_zebes_titles(self):
        handles = workflow.list_handles(self.template())

        self.assertEqual(
            set(handles),
            {workflow.SEED, workflow.POSITIVE_PROMPT, workflow.CONTROL_IMAGE},
        )
        self.assertNotIn("Load Checkpoint", handles)

    def test_a_missing_handle_lists_what_the_template_does_expose(self):
        with self.assertRaises(workflow.WorkflowError) as caught:
            workflow.find(self.template(), workflow.IDENTITY_IMAGE)

        message = str(caught.exception)
        self.assertIn(workflow.SEED, message)
        self.assertIn("right-click", message)

    def test_a_duplicated_handle_is_rejected(self):
        # Picking either would make the run depend on dict ordering.
        template = self.template()
        template["7"] = dict(template["6"])

        with self.assertRaises(workflow.WorkflowError) as caught:
            workflow.find(template, workflow.POSITIVE_PROMPT)

        self.assertIn("must be unique", str(caught.exception))

    def test_set_input_replaces_a_value_without_touching_the_original(self):
        template = self.template()

        patched = workflow.set_input(template, workflow.SEED, "seed", 12345)

        self.assertEqual(patched["3"]["inputs"]["seed"], 12345)
        self.assertEqual(template["3"]["inputs"]["seed"], 0)

    def test_set_input_refuses_an_input_the_node_does_not_declare(self):
        with self.assertRaises(workflow.WorkflowError) as caught:
            workflow.set_input(self.template(), workflow.SEED, "denoise", 0.5)

        self.assertIn("has no input 'denoise'", str(caught.exception))

    def test_apply_leaves_the_template_untouched_when_one_patch_fails(self):
        template = self.template()

        with self.assertRaises(workflow.WorkflowError):
            workflow.apply(
                template,
                {
                    workflow.SEED: {"seed": 7},
                    workflow.POSITIVE_PROMPT: {"nonexistent": "x"},
                },
            )

        self.assertEqual(template["3"]["inputs"]["seed"], 0)

    def test_apply_sets_every_requested_value(self):
        patched = workflow.apply(
            self.template(),
            {
                workflow.SEED: {"seed": 99, "steps": 30},
                workflow.CONTROL_IMAGE: {"image": "depth/00-right.png"},
            },
        )

        self.assertEqual(patched["3"]["inputs"]["seed"], 99)
        self.assertEqual(patched["3"]["inputs"]["steps"], 30)
        self.assertEqual(patched["10"]["inputs"]["image"], "depth/00-right.png")

    def test_describe_names_each_handle_and_its_class(self):
        described = workflow.describe(self.template())

        self.assertIn(workflow.SEED, described)
        self.assertIn("KSampler", described)

    def test_describe_explains_an_untitled_template(self):
        described = workflow.describe({"3": {"class_type": "KSampler", "inputs": {}}})

        self.assertIn("no ZEBES_ handles", described)


if __name__ == "__main__":
    unittest.main()
