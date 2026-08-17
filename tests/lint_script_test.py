import os
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parent.parent / "scripts" / "lint.sh"


class LintScriptTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name).resolve()
        (self.root / "scripts").mkdir()
        (self.root / "src").mkdir()
        (self.root / "build" / "dev").mkdir(parents=True)
        (self.root / "build" / "dev" / "compile_commands.json").touch()
        shutil.copy2(SCRIPT_PATH, self.root / "scripts" / "lint.sh")
        self.event_log = self.root / "events.log"
        self.fake_tidy = self.root / "fake-clang-tidy"
        self.write_executable(
            self.fake_tidy,
            """
            #!/usr/bin/env bash
            source_file="${!#}"
            name="$(basename "${source_file}")"
            printf 'start %s\n' "${name}" >>"${EVENT_LOG}"
            sleep 0.2
            printf 'finish %s\n' "${name}" >>"${EVENT_LOG}"
            if [[ "${name}" == "${FAIL_FILE:-}" ]]; then
              printf 'diagnostic for %s\n' "${name}"
              exit 7
            fi
            printf 'success detail for %s\n' "${name}"
            """,
        )
        for name in ("one.cc", "two.cc", "three.cc"):
            (self.root / "src" / name).touch()

    def write_executable(self, path, contents):
        path.write_text(textwrap.dedent(contents).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def run_script(self, names, extra_env=None):
        env = os.environ.copy()
        env.update(
            {
                "CLANG_TIDY": str(self.fake_tidy),
                "EVENT_LOG": str(self.event_log),
            }
        )
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            [str(self.root / "scripts" / "lint.sh")]
            + [f"src/{name}" for name in names],
            cwd=self.root,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_scoped_lint_runs_two_workers_and_reports_concisely(self):
        result = self.run_script(["one.cc", "two.cc", "three.cc"])

        self.assertEqual(result.returncode, 0, result.stderr)
        events = self.event_log.read_text(encoding="utf-8").splitlines()
        self.assertEqual(set(events[:2]), {"start one.cc", "start two.cc"})
        self.assertEqual(events[-2:], ["start three.cc", "finish three.cc"])
        self.assertEqual(events.count("finish one.cc"), 1)
        self.assertEqual(events.count("finish two.cc"), 1)
        self.assertIn("PASS clang-tidy src/one.cc", result.stdout)
        self.assertIn("PASS clang-tidy src/two.cc", result.stdout)
        self.assertIn("PASS clang-tidy src/three.cc", result.stdout)
        self.assertNotIn("success detail", result.stdout)

    def test_scoped_lint_preserves_failure_status_and_output(self):
        result = self.run_script(
            ["one.cc", "two.cc", "three.cc"], {"FAIL_FILE": "two.cc"}
        )

        self.assertEqual(result.returncode, 7)
        self.assertIn("FAIL clang-tidy src/two.cc", result.stderr)
        self.assertIn("diagnostic for two.cc", result.stderr)
        self.assertIn("PASS clang-tidy src/one.cc", result.stdout)
        self.assertIn("PASS clang-tidy src/three.cc", result.stdout)
        events = self.event_log.read_text(encoding="utf-8").splitlines()
        self.assertEqual(events.count("start one.cc"), 1)
        self.assertEqual(events.count("start two.cc"), 1)
        self.assertEqual(events.count("start three.cc"), 1)


if __name__ == "__main__":
    unittest.main()
