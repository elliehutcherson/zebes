# Zebes Style Guide

Zebes follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
with the project-specific rules recorded under [`.claude/rules/`](../.claude/rules/).

**Those files are the rules. This page is the map.** Each rule file is plain
markdown, written to be read by a person as easily as by an agent. They live
under `.claude/` because Claude Code loads them automatically when someone edits
a matching file; that placement is a delivery mechanism, not an audience.

Do not restate a rule here. A second copy drifts, and the copy people read stops
matching the copy agents load.

## Where the rules live

| Topic | File | Applies when you touch |
| --- | --- | --- |
| Naming, types, errors, control flow, headers, layering | [`cpp-style.md`](../.claude/rules/cpp-style.md) | `*.cc`, `*.h`, `CMakeLists.txt` |
| Serialized definition formats, schema migrations | [`definitions.md`](../.claude/rules/definitions.md) | `src/objects/`, `src/resources/`, `scripts/migrate_definitions.py`, `tests/` fixtures |
| Determinism, fakes vs. mocks, headless requirement | [`testing.md`](../.claude/rules/testing.md) | `tests/` |

Two rules sit outside those files:

- Build and verification commands, and the debugging protocol: [`CLAUDE.md`](../CLAUDE.md).
- Cross-layer ownership and lifetime: [`architecture.md`](architecture.md).

## Why the rules are shaped this way

Rationale that would bloat the rule files, kept here for the reader deciding
whether a rule still earns its place.

**No optional fields in serialized formats.** A tolerant reader is a permanent
tax: every future reader has to reason about what an absent field meant, and the
answer changes as the format grows. A migration pays the cost once, at a moment
when someone understands the old data. This is why adding a field means
extending `scripts/migrate_definitions.py` rather than defaulting on read.

**Strict parsing needs backstops.** Demanding every field turns a silent
misparse into a loud failure only if something exercises the failure. Two
mechanisms carry that weight: every shipped definition is loaded by a test, and
every `LoadAll*` reports the files it could not read. Strict parsing without
both is a trap, not an invariant.

**Domain types at library boundaries.** Keeping SDL and ImGui types out of
engine and resource interfaces is what makes the headless test preset possible.
When a test needs a window, that is usually the boundary leaking, not a
legitimate need for a display.

**Fail immediately rather than partially construct.** A half-built object
outlives the error that produced it and fails somewhere unrelated. `absl::Status`
propagation with `RETURN_IF_ERROR` keeps the failure adjacent to its cause.

## Formatting and verification

`.clang-format` is the formatting authority. A `PostToolUse` hook in
[`.claude/settings.json`](../.claude/settings.json) runs `clang-format -i` on
every `.cc` and `.h` file an agent edits, so agent-written code is formatted
without anyone asking. Format your own edits with your editor's clang-format
integration or:

```bash
clang-format -i path/to/file.cc
```

`.clang-tidy` is configured but not installed or wired into the build. Nothing
runs it today. Treat the rule files, not the linter, as the enforcement layer.

Before handing off a change:

```bash
./scripts/build_and_test.sh              # build + headless tests
./scripts/build_and_test.sh --ui-tests   # when the change touches SDL or ImGui
git diff --check                         # trailing whitespace, conflict markers
```

## For other agents

Read every file in `.claude/rules/`. They are unconditional project rules except
for the `paths:` frontmatter, which only tells Claude Code when to load them
lazily; the content applies regardless of which tool is reading.
