import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "affected_tests.py"
SPEC = importlib.util.spec_from_file_location("affected_tests", SCRIPT_PATH)
affected_tests = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = affected_tests
SPEC.loader.exec_module(affected_tests)


class AffectedTestsTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.reply_dir = Path(self._temp.name)
        self.addCleanup(self._temp.cleanup)

    def write_json(self, name, value):
        (self.reply_dir / name).write_text(json.dumps(value), encoding="utf-8")

    def write_codemodel(self, targets):
        references = []
        for name, target in targets.items():
            filename = f"target-{name}.json"
            target_id = f"{name}::id"
            dependencies = [
                {"id": f"{dependency}::id"}
                for dependency in target.get("dependencies", [])
            ]
            value = {
                "id": target_id,
                "name": name,
                "type": target.get("type", "STATIC_LIBRARY"),
                "dependencies": dependencies,
            }
            if target.get("test", False):
                value["type"] = "EXECUTABLE"
                value["artifacts"] = [{"path": f"bin/tests/{name}"}]
            self.write_json(filename, value)
            references.append({"id": target_id, "jsonFile": filename, "name": name})

        self.write_json(
            "codemodel.json", {"configurations": [{"targets": references}]}
        )
        self.write_json(
            "index-test.json",
            {
                "reply": {
                    "client-zebes": {
                        "codemodel-v2": {"jsonFile": "codemodel.json"}
                    }
                }
            },
        )

    def test_finds_direct_and_transitive_dependent_tests(self):
        self.write_codemodel(
            {
                "background_task": {},
                "background_task_test": {
                    "dependencies": ["background_task"],
                    "test": True,
                },
                "terrain_editor": {"dependencies": ["background_task"]},
                "terrain_editor_test": {
                    "dependencies": ["terrain_editor"],
                    "test": True,
                },
                "unrelated_test": {"test": True},
            }
        )

        self.assertEqual(
            affected_tests.find_affected_tests(
                self.reply_dir, ["background_task"]
            ),
            ["background_task_test", "terrain_editor_test"],
        )

    def test_a_test_target_includes_itself(self):
        self.write_codemodel({"unit_test": {"test": True}})

        self.assertEqual(
            affected_tests.find_affected_tests(self.reply_dir, ["unit_test"]),
            ["unit_test"],
        )

    def test_rejects_an_unknown_target(self):
        self.write_codemodel({"known": {"test": True}})

        with self.assertRaisesRegex(
            affected_tests.AffectedTestsError, "Unknown CMake target"
        ):
            affected_tests.find_affected_tests(self.reply_dir, ["missing"])

    def test_rejects_a_target_with_no_tests(self):
        self.write_codemodel({"untested_library": {}})

        with self.assertRaisesRegex(
            affected_tests.AffectedTestsError, "No test executable depends"
        ):
            affected_tests.find_affected_tests(
                self.reply_dir, ["untested_library"]
            )


if __name__ == "__main__":
    unittest.main()
