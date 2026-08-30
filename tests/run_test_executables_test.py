import contextlib
import io
import json
import os
import stat
import tempfile
import unittest
from pathlib import Path

from scripts import run_test_executables


class RunTestExecutablesTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name).resolve()
        self.working = self.root / "working"
        self.working.mkdir()
        self.run_log = self.root / "runs.log"
        self.old_run_log = os.environ.get("RUN_LOG")
        os.environ["RUN_LOG"] = str(self.run_log)
        self.addCleanup(self._restore_environment)

    def _restore_environment(self):
        if self.old_run_log is None:
            os.environ.pop("RUN_LOG", None)
        else:
            os.environ["RUN_LOG"] = self.old_run_log

    def write_executable(self, name: str, status: int = 0) -> Path:
        path = self.root / name
        path.write_text(
            "#!/usr/bin/env bash\n"
            f"printf '{name}\\n' >>\"${{RUN_LOG}}\"\n"
            f"printf 'details from {name}\\n'\n"
            f"exit {status}\n",
            encoding="utf-8",
        )
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def case(self, name: str, executable: Path, *arguments: str) -> dict:
        return {
            "name": name,
            "command": [str(executable), *arguments],
            "properties": [
                {"name": "WORKING_DIRECTORY", "value": str(self.working)}
            ],
        }

    def pre_test_case(
        self, name: str, executable: Path, *, extra_arguments: str = ""
    ) -> dict:
        return {
            "name": name,
            "command": [
                str(self.root / "cmake"),
                "-D",
                f"TEST_EXECUTABLE={executable}",
                "-D",
                "TEST_EXECUTOR=",
                "-D",
                f"TEST_FILTER={name}",
                "-D",
                "TEST_XML_OUTPUT=",
                "-D",
                f"TEST_EXTRA_ARGS={extra_arguments}",
                "-P",
                "/cmake/Modules/GoogleTest/LaunchTest.cmake",
            ],
            "properties": [
                {"name": "WORKING_DIRECTORY", "value": str(self.working)}
            ],
        }

    def test_groups_discovered_cases_and_runs_each_binary_once(self):
        first = self.write_executable("first_test")
        second = self.write_executable("second_test")
        document = {
            "tests": [
                self.case("First.One", first, "--gtest_filter=First.One"),
                self.case(
                    "First.Two",
                    first,
                    "--gtest_filter=First.Two",
                    "--gtest_also_run_disabled_tests",
                ),
                self.case("second_test", second),
            ]
        }

        grouped = run_test_executables.group_test_executables(document)
        with contextlib.redirect_stdout(io.StringIO()):
            status = run_test_executables.run_executables(grouped)

        self.assertEqual(status, 0)
        self.assertEqual(
            self.run_log.read_text(encoding="utf-8").splitlines(),
            ["first_test", "second_test"],
        )
        self.assertEqual(grouped[0].test_names, ["First.One", "First.Two"])

    def test_groups_pre_test_launch_commands_and_runs_each_binary_once(self):
        executable = self.write_executable("pre_test")
        document = {
            "tests": [
                self.pre_test_case("Suite.One", executable),
                self.pre_test_case("Suite.Two", executable),
            ]
        }

        grouped = run_test_executables.group_test_executables(document)
        with contextlib.redirect_stdout(io.StringIO()):
            status = run_test_executables.run_executables(grouped)

        self.assertEqual(status, 0)
        self.assertEqual(
            self.run_log.read_text(encoding="utf-8").splitlines(), ["pre_test"]
        )
        self.assertEqual(grouped[0].test_names, ["Suite.One", "Suite.Two"])

    def test_failure_runs_remaining_binaries_and_prints_complete_output(self):
        first = self.write_executable("first_test", status=7)
        second = self.write_executable("second_test")
        grouped = run_test_executables.group_test_executables(
            {"tests": [self.case("First.One", first), self.case("Second.One", second)]}
        )
        error = io.StringIO()

        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(error):
            status = run_test_executables.run_executables(grouped)

        self.assertEqual(status, 1)
        self.assertEqual(
            self.run_log.read_text(encoding="utf-8").splitlines(),
            ["first_test", "second_test"],
        )
        self.assertIn("FAIL first_test (exit 7)", error.getvalue())
        self.assertIn("details from first_test", error.getvalue())

    def test_quiet_mode_prints_only_final_success_summary(self):
        executable = self.write_executable("quiet_test")
        grouped = run_test_executables.group_test_executables(
            {"tests": [self.case("Quiet.One", executable)]}
        )
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            status = run_test_executables.run_executables(grouped, quiet=True)

        self.assertEqual(status, 0)
        self.assertEqual(output.getvalue(), "Passed 1 test executables.\n")

    def test_refuses_custom_test_arguments_instead_of_silently_dropping_them(self):
        executable = self.write_executable("custom_test")
        document = {
            "tests": [self.case("custom", executable, "--required-custom-argument")]
        }

        with self.assertRaisesRegex(
            run_test_executables.TestRunnerError, "custom arguments"
        ):
            run_test_executables.group_test_executables(document)

    def test_refuses_pre_test_semantics_it_cannot_preserve(self):
        executable = self.write_executable("custom_pre_test")
        document = {
            "tests": [
                self.pre_test_case(
                    "Suite.Custom", executable, extra_arguments="--required-custom-argument"
                )
            ]
        }

        with self.assertRaisesRegex(
            run_test_executables.TestRunnerError, "unsupported TEST_EXTRA_ARGS"
        ):
            run_test_executables.group_test_executables(document)

    def test_refuses_one_binary_registered_with_two_working_directories(self):
        executable = self.write_executable("test")
        first = self.case("First.One", executable)
        second = self.case("First.Two", executable)
        second["properties"][0]["value"] = str(self.root)

        with self.assertRaisesRegex(
            run_test_executables.TestRunnerError, "multiple working directories"
        ):
            run_test_executables.group_test_executables({"tests": [first, second]})

    def test_refuses_execution_properties_it_cannot_preserve(self):
        executable = self.write_executable("test")
        case = self.case("test", executable)
        case["properties"].append({"name": "ENVIRONMENT", "value": ["MODE=strict"]})

        with self.assertRaisesRegex(
            run_test_executables.TestRunnerError, "unsupported execution properties"
        ):
            run_test_executables.group_test_executables({"tests": [case]})


if __name__ == "__main__":
    unittest.main()
