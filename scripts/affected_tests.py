#!/usr/bin/env python3
"""Find test executables that transitively depend on CMake targets."""

import argparse
import json
import sys
from collections import defaultdict, deque
from pathlib import Path


CLIENT_NAME = "client-zebes"


class AffectedTestsError(Exception):
    """The CMake codemodel cannot provide a safe affected-test set."""


def _load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AffectedTestsError(f"Could not read CMake codemodel file {path}: {error}") from error


def _find_index(reply_dir):
    indexes = list(reply_dir.glob("index-*.json"))
    if not indexes:
        raise AffectedTestsError(f"No CMake File API index exists in {reply_dir}")
    return max(indexes, key=lambda path: path.stat().st_mtime_ns)


def _codemodel_path(reply_dir):
    index = _load_json(_find_index(reply_dir))
    try:
        filename = index["reply"][CLIENT_NAME]["codemodel-v2"]["jsonFile"]
    except (KeyError, TypeError) as error:
        raise AffectedTestsError(
            f"The latest CMake File API reply has no {CLIENT_NAME} codemodel"
        ) from error
    return reply_dir / filename


def _is_test_executable(target):
    if target.get("type") != "EXECUTABLE":
        return False
    return any(
        Path(artifact["path"]).parts[:2] == ("bin", "tests")
        for artifact in target.get("artifacts", [])
        if "path" in artifact
    )


def find_affected_tests(reply_dir, target_names):
    """Return test target names in the reverse dependency closure."""
    codemodel = _load_json(_codemodel_path(reply_dir))
    configurations = codemodel.get("configurations", [])
    if len(configurations) != 1:
        raise AffectedTestsError(
            f"Expected one CMake configuration, found {len(configurations)}"
        )

    references = configurations[0].get("targets", [])
    targets = {}
    ids_by_name = {}
    reverse_dependencies = defaultdict(set)
    test_ids = set()

    for reference in references:
        target = _load_json(reply_dir / reference["jsonFile"])
        target_id = reference["id"]
        targets[target_id] = target
        ids_by_name[reference["name"]] = target_id
        if _is_test_executable(target):
            test_ids.add(target_id)

    for target_id, target in targets.items():
        for dependency in target.get("dependencies", []):
            reverse_dependencies[dependency["id"]].add(target_id)

    missing = sorted(set(target_names) - ids_by_name.keys())
    if missing:
        raise AffectedTestsError(f"Unknown CMake target(s): {', '.join(missing)}")

    affected = set()
    pending = deque(ids_by_name[name] for name in target_names)
    while pending:
        target_id = pending.popleft()
        if target_id in affected:
            continue
        affected.add(target_id)
        pending.extend(reverse_dependencies[target_id])

    tests = sorted(targets[target_id]["name"] for target_id in affected & test_ids)
    if not tests:
        names = ", ".join(target_names)
        raise AffectedTestsError(f"No test executable depends on CMake target(s): {names}")
    return tests


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reply-dir", required=True, type=Path)
    parser.add_argument("targets", nargs="+")
    args = parser.parse_args()

    try:
        tests = find_affected_tests(args.reply_dir, args.targets)
    except AffectedTestsError as error:
        print(f"affected_tests.py: {error}", file=sys.stderr)
        return 2

    print("\n".join(tests))
    return 0


if __name__ == "__main__":
    sys.exit(main())
