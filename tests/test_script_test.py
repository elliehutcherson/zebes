import os
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "test.sh"


class TestScriptTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name)
        (self.root / "scripts").mkdir()
        (self.root / "fake-bin").mkdir()
        (self.root / "build" / "dev" / "bin" / "tests").mkdir(
            parents=True
        )
        shutil.copy2(SCRIPT_PATH, self.root / "scripts" / "test.sh")
        (self.root / "scripts" / "affected_tests.py").touch()
        self.command_log = self.root / "commands.log"
        self.run_log = self.root / "runs.log"
        self.write_executable(
            self.root / "fake-bin" / "cmake",
            """
            #!/usr/bin/env bash
            printf 'cmake' >>"${COMMAND_LOG}"
            printf ' <%s>' "$@" >>"${COMMAND_LOG}"
            printf '\n' >>"${COMMAND_LOG}"
            if [[ "${1:-}" == "--build" ]]; then
              printf 'build details\n'
              exit "${BUILD_STATUS:-0}"
            fi
            mkdir -p "${PROJECT_ROOT}/build/dev/.cmake/api/v1/reply"
            printf 'configure details\n'
            exit "${CONFIGURE_STATUS:-0}"
            """,
        )
        self.write_executable(
            self.root / "fake-bin" / "python3",
            """
            #!/usr/bin/env bash
            printf 'first_test\nsecond_test\n'
            """,
        )

    def write_executable(self, path, contents):
        path.write_text(textwrap.dedent(contents).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def write_test_binary(self, name, body):
        self.write_executable(
            self.root / "build" / "dev" / "bin" / "tests" / name,
            f"""
            #!/usr/bin/env bash
            printf '{name}\\n' >>"${{RUN_LOG}}"
            {body}
            """,
        )

    def run_script(self, extra_env=None):
        env = os.environ.copy()
        env.update(
            {
                "PATH": f"{self.root / 'fake-bin'}:{env['PATH']}",
                "COMMAND_LOG": str(self.command_log),
                "PROJECT_ROOT": str(self.root),
                "RUN_LOG": str(self.run_log),
            }
        )
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            [
                str(self.root / "scripts" / "test.sh"),
                "--affected-target",
                "prop_artwork",
            ],
            cwd=self.root,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_affected_targets_are_built_together_and_run_once(self):
        self.write_test_binary("first_test", "printf 'first details\\n'")
        self.write_test_binary("second_test", "printf 'second details\\n'")

        result = self.run_script()

        self.assertEqual(result.returncode, 0, result.stderr)
        command_lines = self.command_log.read_text(encoding="utf-8").splitlines()
        build_lines = [line for line in command_lines if " <--build>" in line]
        self.assertEqual(len(build_lines), 1)
        self.assertIn(" <--target> <first_test> <second_test>", build_lines[0])
        self.assertEqual(
            self.run_log.read_text(encoding="utf-8").splitlines(),
            ["first_test", "second_test"],
        )
        self.assertIn("PASS first_test", result.stdout)
        self.assertIn("PASS second_test", result.stdout)
        self.assertNotIn("first details", result.stdout)
        self.assertNotIn("second details", result.stdout)

    def test_test_failure_preserves_status_and_complete_output(self):
        self.write_test_binary(
            "first_test",
            "printf 'stdout detail\\n'; printf 'stderr detail\\n' >&2; exit 7",
        )
        self.write_test_binary("second_test", "printf 'should not run\\n'")

        result = self.run_script()

        self.assertEqual(result.returncode, 7)
        self.assertIn("FAIL first_test", result.stderr)
        self.assertIn("stdout detail", result.stderr)
        self.assertIn("stderr detail", result.stderr)
        self.assertEqual(
            self.run_log.read_text(encoding="utf-8").splitlines(),
            ["first_test"],
        )

    def test_build_failure_preserves_status_and_complete_output(self):
        self.write_test_binary("first_test", "exit 0")
        self.write_test_binary("second_test", "exit 0")

        result = self.run_script({"BUILD_STATUS": "9"})

        self.assertEqual(result.returncode, 9)
        self.assertIn("Failed to build affected tests", result.stderr)
        self.assertIn("build details", result.stderr)
        self.assertFalse(self.run_log.exists())

    def test_configure_failure_preserves_status_and_complete_output(self):
        self.write_test_binary("first_test", "exit 0")
        self.write_test_binary("second_test", "exit 0")

        result = self.run_script({"CONFIGURE_STATUS": "8"})

        self.assertEqual(result.returncode, 8)
        self.assertIn("Failed to configure dev test build", result.stderr)
        self.assertIn("configure details", result.stderr)
        self.assertEqual(
            self.command_log.read_text(encoding="utf-8").count("cmake"), 1
        )
        self.assertFalse(self.run_log.exists())


if __name__ == "__main__":
    unittest.main()
