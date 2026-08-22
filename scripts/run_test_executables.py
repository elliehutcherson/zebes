#!/usr/bin/env python3
"""Run each CTest-registered test executable once.

GoogleTest discovery gives CTest excellent case-level reporting, but a full
local gate pays for a fresh process for every discovered case. This runner uses
CTest's JSON manifest as the source of truth, groups those cases by executable,
and runs each binary once from the same working directory. It deliberately runs
serially: several resource-manager suites use fixed test directories, and the
SDL/ImGui integration tests must not compete for the host display.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path


class TestRunnerError(RuntimeError):
    """The CTest manifest cannot be executed safely as whole binaries."""


_SUPPORTED_PROPERTIES = {
    # Preserved directly by this runner.
    "WORKING_DIRECTORY",
    # Applied by CTest while selecting the JSON manifest.
    "LABELS",
    # The runner is serial, which is stronger than these locks.
    "RESOURCE_LOCK",
    # GoogleTest reports skipped cases itself when the whole binary runs.
    "SKIP_REGULAR_EXPRESSION",
    # Discovery metadata with no execution semantics.
    "DEF_SOURCE_LINE",
}


@dataclass
class TestExecutable:
    path: Path
    working_directory: Path
    test_names: list[str] = field(default_factory=list)


def _property(test: dict, name: str):
    for item in test.get("properties", []):
        if item.get("name") == name:
            return item.get("value")
    return None


def _is_supported_case_command(arguments: list[str]) -> bool:
    return all(
        argument == "--gtest_also_run_disabled_tests"
        or argument.startswith("--gtest_filter=")
        for argument in arguments
    )


def group_test_executables(document: dict) -> list[TestExecutable]:
    """Groups a CTest JSON document without guessing about custom commands."""
    tests = document.get("tests")
    if not isinstance(tests, list):
        raise TestRunnerError("CTest JSON does not contain a test list")

    grouped: OrderedDict[Path, TestExecutable] = OrderedDict()
    for test in tests:
        name = test.get("name")
        command = test.get("command")
        if not isinstance(name, str) or not name:
            raise TestRunnerError("CTest returned a test without a name")
        if not isinstance(command, list) or not command or not all(
            isinstance(argument, str) for argument in command
        ):
            raise TestRunnerError(f"CTest test '{name}' has an invalid command")
        if not _is_supported_case_command(command[1:]):
            raise TestRunnerError(
                f"CTest test '{name}' has custom arguments and cannot be grouped: "
                + " ".join(command[1:])
            )

        property_names = {
            item.get("name") for item in test.get("properties", []) if isinstance(item, dict)
        }
        unsupported = sorted(property_names - _SUPPORTED_PROPERTIES)
        if unsupported:
            raise TestRunnerError(
                f"CTest test '{name}' has unsupported execution properties: "
                + ", ".join(unsupported)
            )

        executable = Path(command[0]).resolve()
        working_value = _property(test, "WORKING_DIRECTORY")
        if not isinstance(working_value, str) or not working_value:
            raise TestRunnerError(f"CTest test '{name}' has no working directory")
        working_directory = Path(working_value).resolve()

        existing = grouped.get(executable)
        if existing is None:
            grouped[executable] = TestExecutable(
                path=executable,
                working_directory=working_directory,
                test_names=[name],
            )
            continue
        if existing.working_directory != working_directory:
            raise TestRunnerError(
                f"CTest registers '{executable}' with multiple working directories"
            )
        existing.test_names.append(name)

    if not grouped:
        raise TestRunnerError("CTest selected no tests")
    return list(grouped.values())


def load_ctest_manifest(build_dir: Path, label: str | None) -> dict:
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--show-only=json-v1",
    ]
    if label is not None:
        command.extend(["-L", label])
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise TestRunnerError(
            "Could not read the CTest manifest:\n" + completed.stdout + completed.stderr
        )
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise TestRunnerError(f"CTest returned invalid JSON: {error}") from error
    if document.get("kind") != "ctestInfo":
        raise TestRunnerError("CTest returned an unexpected JSON document")
    return document


def run_executables(executables: list[TestExecutable]) -> int:
    failures: list[str] = []
    for executable in executables:
        if not executable.path.is_file():
            raise TestRunnerError(f"Test executable does not exist: {executable.path}")
        if not executable.working_directory.is_dir():
            raise TestRunnerError(
                f"Test working directory does not exist: {executable.working_directory}"
            )

        completed = subprocess.run(
            [str(executable.path)],
            cwd=executable.working_directory,
            capture_output=True,
            text=True,
            check=False,
        )
        label = executable.path.name
        if completed.returncode == 0:
            print(f"PASS {label} ({len(executable.test_names)} registered tests)")
            continue

        failures.append(label)
        print(f"FAIL {label} (exit {completed.returncode})", file=sys.stderr)
        if completed.stdout:
            print(completed.stdout, end="", file=sys.stderr)
        if completed.stderr:
            print(completed.stderr, end="", file=sys.stderr)

    if failures:
        print(
            f"Failed {len(failures)} of {len(executables)} test executables: "
            + ", ".join(failures),
            file=sys.stderr,
        )
        return 1
    print(f"Passed {len(executables)} test executables.")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--label", help="Run only tests carrying this CTest label")
    args = parser.parse_args(argv)

    try:
        document = load_ctest_manifest(args.build_dir.resolve(), args.label)
        return run_executables(group_test_executables(document))
    except TestRunnerError as error:
        print(f"run_test_executables.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
