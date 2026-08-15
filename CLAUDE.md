# Zebes

C++ game engine. SDL for platform, ImGui for the editor, Abseil throughout.

## Build

CMake only. Do not use Bazel.

```bash
./scripts/build_and_test.sh              # build + tests
./scripts/build_and_test.sh --ui-tests   # adds SDL/ImGui tests
```

Finish every task by running `./scripts/build_and_test.sh` and confirming all
targets compile and all tests pass. Use `--ui-tests` when the change touches
SDL or ImGui behavior. Do not report a task complete on an unverified build.

## Debugging

Do not permute syntax hoping for a fix. Changing brace initialization to
constructor calls, reordering fields, or swapping types at random is not
debugging.

1. Read the failure log and test output before changing anything.
2. If the cause is unclear, add `LOG(INFO)` statements or counters to trace
   execution. Instrument first, then change logic.
3. If analysis and instrumentation do not identify the cause, stop and ask.
   Say "I am stuck on this test failure" or "How is this intended to work?"
   Do not guess.

A test must fail when required setup or data is missing. Never guard an
assertion with an `if` to make a test pass.

## Reference

- Style, layering, error handling: `.claude/rules/`
- Cross-layer ownership and lifetime rules: `docs/architecture.md`. Update it
  when adding or changing an architectural boundary.