#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path


DEPENDENCY_FLAGS = {"-M", "-MD", "-MG", "-MM", "-MMD", "-MP"}
DEPENDENCY_OPTIONS = {"-MF", "-MJ", "-MQ", "-MT", "-o"}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compute safe cache keys for scoped clang-tidy runs."
    )
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--clang-tidy", required=True, type=Path)
    parser.add_argument("--state-dir", required=True, type=Path)
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--extra-arg", action="append", default=[])
    parser.add_argument("sources", nargs="+", type=Path)
    return parser.parse_args()


def read_compile_commands(build_dir):
    with (build_dir / "compile_commands.json").open(encoding="utf-8") as commands_file:
        commands = json.load(commands_file)

    by_source = {}
    for command in commands:
        source = str(Path(command["file"]).resolve())
        by_source.setdefault(source, command)
    return by_source


def command_arguments(command):
    if "arguments" in command:
        return list(command["arguments"])
    return shlex.split(command["command"])


def dependency_command(command, source, extra_args):
    arguments = command_arguments(command)
    if not arguments:
        raise ValueError("compile command is empty")

    source = source.resolve()
    directory = Path(command["directory"])
    filtered = [arguments[0]]
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        if argument in DEPENDENCY_OPTIONS:
            index += 2
            continue
        if argument in DEPENDENCY_FLAGS or argument == "-c":
            index += 1
            continue
        if any(
            argument.startswith(option) and argument != option
            for option in DEPENDENCY_OPTIONS
        ):
            index += 1
            continue

        candidate = Path(argument)
        if not argument.startswith("-"):
            if not candidate.is_absolute():
                candidate = directory / candidate
            if candidate.resolve() == source:
                index += 1
                continue
        filtered.append(argument)
        index += 1

    filtered.extend(extra_args)
    filtered.extend(["-M", "-MG", "-MT", "zebes_lint_cache", str(source)])
    return filtered


def parse_dependencies(output, directory):
    flattened = output.replace("\\\n", " ")
    separator = flattened.find(":")
    if separator < 0:
        raise ValueError("compiler dependency output has no target separator")

    dependencies = []
    for dependency in shlex.split(flattened[separator + 1 :]):
        path = Path(dependency)
        if not path.is_absolute():
            path = directory / path
        dependencies.append(path.resolve())
    return dependencies


def discover_dependencies(command, source, extra_args):
    directory = Path(command["directory"])
    result = subprocess.run(
        dependency_command(command, source, extra_args),
        cwd=directory,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr)
    return parse_dependencies(result.stdout, directory)


def digest_file(path, digest_cache):
    try:
        stat = path.stat()
    except OSError:
        return hashlib.sha256(f"missing:{path}".encode()).digest()

    cache_key = (path, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns)
    if cache_key in digest_cache:
        return digest_cache[cache_key]

    digest = hashlib.sha256()
    with path.open("rb") as source_file:
        for chunk in iter(lambda: source_file.read(1024 * 1024), b""):
            digest.update(chunk)
    result = digest.digest()
    digest_cache[cache_key] = result
    return result


def file_metadata(path):
    stat = path.stat()
    return {
        "path": str(path),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "ctime_ns": stat.st_ctime_ns,
    }


def base_digest(args, digest_cache):
    digest = hashlib.sha256()
    digest.update(b"zebes-clang-tidy-cache-v1\0")
    digest.update(b"strict\0" if args.strict else b"non-strict\0")
    for extra_arg in args.extra_arg:
        digest.update(extra_arg.encode())
        digest.update(b"\0")
    for variable in (
        "C_INCLUDE_PATH",
        "CPATH",
        "CPLUS_INCLUDE_PATH",
        "MACOSX_DEPLOYMENT_TARGET",
        "OBJC_INCLUDE_PATH",
        "SDKROOT",
    ):
        digest.update(variable.encode())
        digest.update(b"=")
        digest.update(os.environ.get(variable, "").encode())
        digest.update(b"\0")
    for path in (
        args.project_root / ".clang-tidy",
        args.project_root / "scripts" / "lint.sh",
        args.clang_tidy,
        Path(__file__),
    ):
        resolved = path.resolve()
        digest.update(str(resolved).encode())
        digest.update(b"\0")
        digest.update(digest_file(resolved, digest_cache))
    return digest.digest()


def manifest_path(args, source):
    identity = hashlib.sha256()
    identity.update(str(source).encode())
    identity.update(b"\0strict" if args.strict else b"\0non-strict")
    return args.state_dir / "manifests" / f"{identity.hexdigest()}.json"


def source_signature(args, command, source, shared_digest, digest_cache):
    digest = hashlib.sha256(shared_digest)
    digest.update(str(source).encode())
    digest.update(b"\0")
    digest.update(json.dumps(command, sort_keys=True).encode())
    directory = source.parent
    while True:
        config = directory / ".clang-tidy"
        if config.is_file() and config != args.project_root / ".clang-tidy":
            digest.update(str(config).encode())
            digest.update(b"\0")
            digest.update(digest_file(config, digest_cache))
        if directory == args.project_root or directory.parent == directory:
            break
        directory = directory.parent
    return digest.hexdigest()


def read_manifest(args, source, signature):
    try:
        with manifest_path(args, source).open(encoding="utf-8") as manifest_file:
            manifest = json.load(manifest_file)
        if manifest.get("signature") != signature:
            return None
        for expected in manifest["dependencies"]:
            actual = file_metadata(Path(expected["path"]))
            if actual != expected:
                return None
        key = manifest["key"]
        if (
            not isinstance(key, str)
            or len(key) != 64
            or any(character not in "0123456789abcdef" for character in key)
        ):
            return None
        return key
    except (KeyError, OSError, TypeError, ValueError, json.JSONDecodeError):
        return None


def write_manifest(args, source, signature, dependencies, key):
    path = manifest_path(args, source)
    path.parent.mkdir(parents=True, exist_ok=True)
    manifest = {
        "signature": signature,
        "dependencies": [file_metadata(dependency) for dependency in dependencies],
        "key": key,
    }
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    with temporary.open("w", encoding="utf-8") as manifest_file:
        json.dump(manifest, manifest_file, sort_keys=True, separators=(",", ":"))
    temporary.replace(path)


def cache_key(args, command, source, shared_digest, digest_cache):
    signature = source_signature(args, command, source, shared_digest, digest_cache)
    cached = read_manifest(args, source, signature)
    if cached is not None:
        return cached

    dependencies = discover_dependencies(command, source, args.extra_arg)
    digest = hashlib.sha256(signature.encode())
    for dependency in sorted(set(dependencies)):
        digest.update(str(dependency).encode())
        digest.update(b"\0")
        digest.update(digest_file(dependency, digest_cache))
    key = digest.hexdigest()
    write_manifest(args, source, signature, sorted(set(dependencies)), key)
    return key


def main():
    args = parse_args()
    args.project_root = args.project_root.resolve()
    args.build_dir = args.build_dir.resolve()
    args.clang_tidy = args.clang_tidy.resolve()
    args.state_dir = args.state_dir.resolve()

    try:
        commands = read_compile_commands(args.build_dir)
        digest_cache = {}
        shared_digest = base_digest(args, digest_cache)
    except (OSError, ValueError, json.JSONDecodeError):
        for _ in args.sources:
            print("-")
        return

    for source in args.sources:
        source = source.resolve()
        command = commands.get(str(source))
        if command is None:
            print("-")
            continue
        try:
            print(cache_key(args, command, source, shared_digest, digest_cache))
        except (OSError, RuntimeError, ValueError):
            print("-")


if __name__ == "__main__":
    main()
