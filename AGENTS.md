# Zebes collaboration and engineering guidelines

Treat proposed implementations—including the user's proposals—as design input,
not as unquestionable requirements. Evaluate them against the architecture and
push back constructively when they would weaken maintainability, scalability,
performance, readability, safety, or established boundaries. Explain the
conflict and offer better options with their tradeoffs instead of silently
following a questionable direction.

Prefer code that:

- fails fast with a clear error when invariants or required state are invalid;
- does not continue in a partial, ambiguous, or undefined state;
- keeps control flow flat with guard clauses and early returns;
- makes ownership, lifetime, and subsystem boundaries explicit;
- favors simple, readable designs and removes unnecessary code;
- validates behavior with focused tests at platform-neutral boundaries.

Diagnose failures from their logs and focused instrumentation. Do not permute
syntax hoping for a fix or weaken an assertion to make a test pass.

## Repository boundaries

- First-party implementation and tests live under `src/`, `tests/`, `scripts/`,
  and `cmake/`.
- `include/` contains vendored submodules. Do not search or edit it unless the
  task explicitly concerns a dependency; use `rg --no-ignore` for that case.
- `build/` is generated output, `notes/` contains non-normative research, and
  `old/` is archival. None is a source of project requirements. `.ignore`
  keeps all three, and `include/`, out of default `rg` results.

## Editing and verification

Use the native patch or edit tool for repository changes. Do not rewrite files
with Python, Perl, sed, awk, or shell redirection. Generated files are the only
exception: update their source of truth and run the documented generator.

Do not run tests merely because a session started. During implementation, run
the narrowest test that exercises the changed behavior:

```bash
scripts/test.sh --list
scripts/test.sh terrain_generator_test
scripts/test.sh terrain_generator_test TerrainGeneratorTest.EverySlopeShapeRenders
scripts/test.sh --ui sanity_test
```

Before handoff, run the complete affected test executable and check the patch
with `git diff --check`. Use `scripts/test.sh --affected-target <target>` when
a changed CMake target has several consumers. Run `scripts/build_and_test.sh`
locally only when the affected set cannot be bounded confidently, such as
serialization changes, broadly consumed headers, central build/toolchain logic,
or broad refactors, or when the user explicitly requests it. A header or CMake
edit with a small known consumer set is not inherently cross-cutting. Use the
focused `test.sh --ui` form for SDL or ImGui work. `build_and_test.sh
--ui-tests` narrows the suite to the UI-labeled tests and skips everything
else; `build_and_test.sh --all-tests-with-ui` is the only local command that
matches the GitHub Actions merge gate.

Run `clang-format -i` on edited C++ source and header files before linting.
Pass all edited translation units to one
`scripts/lint.sh <file.cc> [file.cc ...]` invocation so setup is shared and the
analyses run concurrently. For a header, lint representative `.cc` files that
include it. Reserve
`scripts/lint.sh --all` for CI and explicit cleanup milestones; the command
deliberately uses only two workers by default to limit heat and contention.

## Code review rules

- Domain and engine interfaces use Zebes-owned types. SDL and ImGui types stay
  behind platform or editor adapters.
- Serialized format changes require a migration and tests that load every
  shipped definition.
- Do not manually construct texture handles or retain them beyond their owning
  resource store's lifetime.

## Project references

- `docs/style-guide.md` is the style source of truth. `.claude/rules/` is
  generated from it by `scripts/sync_rules.py`; never edit generated rules.
- `docs/architecture.md` records cross-layer ownership and lifetime boundaries.
- `docs/roadmap.md` records remaining work and settled decisions.

Agreement is not the goal. Sound engineering judgment and a maintainable Zebes
codebase are the goal. Ask for clarification only when the choice materially
changes behavior and cannot be resolved from the code or these principles.
