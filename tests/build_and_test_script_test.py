import os
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "build_and_test.sh"


class BuildAndTestScriptTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name).resolve()
        (self.root / "scripts").mkdir()
        (self.root / "fake-bin").mkdir()
        (self.root / "build" / "tileset-venv" / "bin").mkdir(parents=True)
        shutil.copy2(SCRIPT_PATH, self.root / "scripts" / "build_and_test.sh")
        (self.root / "scripts" / "sync_rules.py").touch()
        (self.root / "scripts" / "run_test_executables.py").touch()
        (self.root / "scripts" / "requirements-tileset.txt").touch()
        self.command_log = self.root / "commands.log"

        self.write_executable(
            self.root / "fake-bin" / "cmake",
            """
            #!/usr/bin/env bash
            printf 'cmake' >>"${COMMAND_LOG}"
            printf ' <%s>' "$@" >>"${COMMAND_LOG}"
            printf '\n' >>"${COMMAND_LOG}"
            exit 0
            """,
        )
        self.write_executable(
            self.root / "fake-bin" / "ctest",
            """
            #!/usr/bin/env bash
            printf 'ctest' >>"${COMMAND_LOG}"
            printf ' <%s>' "$@" >>"${COMMAND_LOG}"
            printf '\n' >>"${COMMAND_LOG}"
            exit 0
            """,
        )
        self.write_executable(
            self.root / "fake-bin" / "python3",
            """
            #!/usr/bin/env bash
            printf 'python3' >>"${COMMAND_LOG}"
            printf ' <%s>' "$@" >>"${COMMAND_LOG}"
            printf '\n' >>"${COMMAND_LOG}"
            if [[ "${1:-}" == *run_test_executables.py ]]; then
              exit "${RUNNER_STATUS:-0}"
            fi
            exit 0
            """,
        )
        self.write_executable(
            self.root / "build" / "tileset-venv" / "bin" / "python",
            """
            #!/usr/bin/env bash
            printf 'venv-python' >>"${COMMAND_LOG}"
            printf ' <%s>' "$@" >>"${COMMAND_LOG}"
            printf '\n' >>"${COMMAND_LOG}"
            exit 0
            """,
        )

    def write_executable(self, path: Path, contents: str):
        path.write_text(textwrap.dedent(contents).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def run_script(self, *arguments: str, extra_env=None):
        env = os.environ.copy()
        env.update(
            {
                "PATH": f"{self.root / 'fake-bin'}:{env['PATH']}",
                "COMMAND_LOG": str(self.command_log),
            }
        )
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            [str(self.root / "scripts" / "build_and_test.sh"), *arguments],
            cwd=self.root,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def command_lines(self):
        return self.command_log.read_text(encoding="utf-8").splitlines()

    def test_full_ui_gate_uses_eight_worker_preset_and_binary_runner(self):
        result = self.run_script("--all-tests-with-ui")

        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.command_lines()
        self.assertIn("cmake <--preset> <ui> <-S> " + f"<{self.root}>", commands)
        self.assertIn("cmake <--build> <--preset> <ui-full>", commands)
        runner = next(line for line in commands if "run_test_executables.py" in line)
        self.assertIn(f"<--build-dir> <{self.root / 'build' / 'ui'}>", runner)
        self.assertNotIn("<--label>", runner)
        self.assertFalse(any(line.startswith("ctest") for line in commands))

    def test_ui_only_gate_filters_the_binary_manifest_by_label(self):
        result = self.run_script("--ui-tests")

        self.assertEqual(result.returncode, 0, result.stderr)
        runner = next(
            line for line in self.command_lines() if "run_test_executables.py" in line
        )
        self.assertIn("<--label> <ui>", runner)

    def test_case_filter_retains_ctest_case_level_semantics(self):
        result = self.run_script("--test-filter", "Parallax")

        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.command_lines()
        self.assertIn("cmake <--build> <--preset> <dev-full>", commands)
        self.assertIn("ctest <--preset> <dev> <-R> <Parallax>", commands)
        self.assertFalse(any("run_test_executables.py" in line for line in commands))

    def test_binary_runner_failure_is_not_hidden(self):
        result = self.run_script(extra_env={"RUNNER_STATUS": "7"})

        self.assertEqual(result.returncode, 7)
        self.assertFalse(any(line.startswith("venv-python") for line in self.command_lines()))


if __name__ == "__main__":
    unittest.main()
