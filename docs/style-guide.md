# Zebes Style Guide

Zebes follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
with the project-specific rules below. This is the guide for people working on
Zebes, and it is the source of truth for the rules it states.

Some sections are marked up so that `scripts/sync_rules.py` can extract them into
`.claude/rules/`, where Claude Code loads them automatically when someone edits a
matching file. Those generated files are copies. **Edit this document, then run:**

```bash
scripts/sync_rules.py
```

`scripts/build_and_test.sh` fails if the copies are stale, so the two cannot
drift apart. Never edit a file in `.claude/rules/` directly; the next
regeneration overwrites it.

---

<!-- rule:cpp-style -->

## C++

The [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html),
plus the rules below. Formatting and identifier naming are absent on purpose:
`.clang-format` and `.clang-tidy` enforce them and `scripts/lint.sh` reports
violations, so restating them here would only cost context. The one naming rule
no tool checks is that CMake targets and source file names are `snake_case`.

### Types and initialization

- Brace-initialize aggregates and PODs.
- Use explicit types. Use `auto` only for cumbersome types (iterators) or when
  the expression already names the type (`std::make_unique<Foo>()`).
- References for required dependencies. Pointers only for nullable or
  reseatable ones. Never null-check a reference.
- Express ownership with RAII types such as `std::unique_ptr`.
- Prefer Zebes-owned domain types at library boundaries. Do not expose SDL or
  ImGui types from engine or resource interfaces.

### Libraries

Use `absl::Status`, `absl::StatusOr`, `absl::flat_hash_map`, `absl::StrFormat`
over their STL equivalents.

### Errors

- `absl::Status` and `absl::StatusOr` for recoverable failures.
- Use `RETURN_IF_ERROR` whenever an `absl::Status` failure is returned
  unchanged, and `ASSIGN_OR_RETURN` whenever a `StatusOr` failure is returned
  unchanged and its value is consumed. This includes a status already stored in
  a local variable. Write an explicit `.ok()` branch only when it translates,
  logs, aggregates, or compensates for the error, or intentionally converts the
  failure into non-error control flow.
- Do not use `try`/`catch` in domain, engine, or editor logic. When an external
  or standard-library API can only report failure by throwing, translate that
  exception to `absl::Status` inside the narrow common/resource adapter that
  owns the API. Callers stay entirely in the status error model.
- Fail immediately. Never return a partially constructed object or fall back to
  a default state.

### Control flow

- Guard clauses and early returns over `if`/`else`.
- No `else` after a branch that returns.
- More than two levels of indentation means extract a private helper.

### Comparison and numeric boundaries

- Define `operator<=>` only when a type has one natural, domain-wide ordering.
  Keep presentation and algorithm-specific order explicit at the sort call;
  prefer a ranges projection over a one-field comparison lambda.
- Floating-point aggregates do not get a defaulted `operator<=>`: NaN makes
  their ordering partial. Validate finite values at boundaries that require an
  ordered key.
- Use `std::in_range<T>` before integral narrowing. Keep
  `std::numeric_limits` where the limit itself is meaningful, such as overflow
  arithmetic, exhaustion, infinity, or an intentional boundary test. Do not
  hide it behind a generic maximum-value alias.

### Headers

- Include what the file uses. Do not rely on transitive includes.
- Include type declarations directly by default. Forward declare only when it
  materially cuts coupling or build time.

### Comments

A comment earns its place by saying something the code cannot. Restating the
next statement in English costs the reader attention and returns nothing, and a
file full of such comments trains the reader to skip all of them, including the
one that mattered. Delete instead of trimming when there is nothing to say.

Explanation belongs above the declaration, in the header. A class whose use has
a contract — an ordering rule, a protocol, a threading or lifetime constraint,
what happens on timeout or failure — gets a paragraph above the class, written
in plain prose for someone who has not read the implementation. That is the one
place a long comment pays for itself, because it is where a caller looks.

Inside a function body, comment only the surprising: a non-obvious ordering
constraint, a workaround and the bug it dodges, a tradeoff, the reason the
cheaper approach fails. A body that needs narration to be followed wants
helpers with names, not comments.

- Document ownership, nullability, lifetime, and error behavior whenever the
  type does not make them obvious.
- Do not label blocks with what they do (`// Create the manager`,
  `// 1. Handle zoom`). Extract a named helper.
- No banner separators or numbered outlines (`// --- 2. Query API ---`). A
  plain grouping label in a long interface is fine when the grouping is not
  already obvious from the names under it.
- Plain sentences. Explain the why; the declaration already states the what.
- Delete commented-out code and stale TODOs rather than carrying them.

### Layering

- Depend on interfaces owned by the layer above an external library.
- SDL implementations live under `src/platform/sdl` or SDL-specific UI code.
- ImGui stays in the editor/UI layer.
- Wire concrete dependencies in the application composition root. No globals,
  no service locators.
- Record new cross-layer ownership or lifetime rules in `docs/architecture.md`.

<!-- /rule -->

<!-- rule:definitions -->

## Serialized definitions

Every field is required. There are no optional fields in this format.

### Reading and writing

- Writers emit every field of the record they write.
- Readers require every field with `.at()`. A missing field is corruption.
  Do not substitute a default, and do not add one to make a load succeed.
- Write collections even when empty. An absent list and an empty list must not
  both be spellings of the same state.
- Read exactly one schema version. Do not carry a translation path for a
  version no file on disk uses.

### Adding a field

Add a migration, not a default:

1. Add the field to the writer and the reader.
2. Extend `scripts/migrate_definitions.py` to populate it in existing files.
3. Run the migration once and commit the migrated definitions.

A tolerant reader reinterprets old data forever. A migration moves it once and
the invariant holds afterward.

### Tagged unions

A discriminator such as `TerrainScheme` selects which variant a record is. Each
variant's fields are required for that variant and absent from the others. The
reader determines the variant before reading variant fields. This is not an
optional field.

### Required backstops

Strict parsing is a trap without both of these:

- Every shipped definition is loaded by a test.
- Every `LoadAll*` reports the files it could not read. It must not log a
  failure and return `absl::OkStatus()`.

<!-- /rule -->

<!-- rule:testing -->

## Testing

### Determinism

Tests must be deterministic. If something is expected to exist, assert that it
exists and fail when it does not.

Never guard an assertion with an `if` to make a test pass. Never skip a case
because setup or data is missing — a missing fixture is a failure, not a
reason to pass silently.

Add tests for the failure paths, not only the success path. Every
`absl::Status` a function can return should have a test that produces it.

### Fakes and mocks

- Fakes for stateful, platform-neutral interfaces.
- Mocks only to verify an interaction that matters. Do not mock a type just to
  hand it to a constructor; use the real type or a fake.

### Headless by default

Engine and resource tests run headless. A test that needs an SDL window or
ImGui interaction belongs in the UI test preset, under the `ui` label.

Needing a window usually means the code under test reaches too far down the
stack. Check whether the dependency can be an interface first.

### Definitions

Every shipped definition file is loaded by a test. Adding a definition means
adding it to that test.

<!-- /rule -->

---

## Tool-enforced conventions

These are real rules; they are written here rather than in the extracted
sections because a tool already catches every violation, so spending session
context on them buys nothing. `scripts/lint.sh` is where you find out.

| Convention | Enforced by |
| --- | --- |
| Types, functions: `PascalCase` | `readability-identifier-naming` |
| Locals, parameters: `snake_case` | `readability-identifier-naming` |
| Data members: `snake_case_` | `readability-identifier-naming` |
| Constants: `kPascalCase` | `readability-identifier-naming` |
| Macros: `UPPER_CASE` | `readability-identifier-naming` |
| `static` on member functions that touch no instance state | `readability-convert-member-functions-to-static` |
| Formatting, include order, header guards | `.clang-format`, `google-*` |

CMake target and source file naming (`snake_case`) has no check, which is why
it stays in the extracted C++ section.

## Why these rules

Rationale that would bloat the rules themselves, kept for the reader deciding
whether one still earns its place. This section is not extracted.

**No optional fields in serialized formats.** A tolerant reader is a permanent
tax: every future reader has to reason about what an absent field meant, and the
answer changes as the format grows. A migration pays the cost once, at a moment
when someone still understands the old data. A tile whose `shape` was absent
once loaded as `kNone` — solid artwork colliding with nothing — which is what a
default on read buys you.

**Strict parsing needs its backstops.** Demanding every field turns a silent
misparse into a loud failure only if something exercises the failure. Two
mechanisms carry that weight: every shipped definition is loaded by a test, and
every `LoadAll*` reports the files it could not read rather than logging and
returning success. Strict parsing without both is a trap, not an invariant.

**Domain types at library boundaries.** Keeping SDL and ImGui types out of
engine and resource interfaces is what makes the headless test preset possible.
When a test needs a window, that is usually the boundary leaking, not a
legitimate need for a display.

**Fail immediately rather than partially construct.** A half-built object
outlives the error that produced it and fails somewhere unrelated, usually in
code that did nothing wrong. `absl::Status` propagation keeps the failure
adjacent to its cause.

## Verification

Use a layered loop. While editing, build and run one affected C++ test
executable; an optional GoogleTest filter makes the inner loop narrower:

```bash
./scripts/test.sh terrain_generator_test
./scripts/test.sh terrain_generator_test TerrainGeneratorTest.EverySlopeShapeRenders
./scripts/test.sh --ui sanity_test
```

Before handoff, run the complete affected test executable, lint each edited
translation unit, and check the patch:

```bash
./scripts/lint.sh src/terrain/terrain_generator.cc
git diff --check
```

Header files have no independent compilation command. For a header change,
lint representative `.cc` files that include it. When a changed CMake target
has several consumers, use `./scripts/test.sh --affected-target <target>` to
run the test executables in its reverse dependency closure.

Run the comprehensive local suite only when the affected set cannot be bounded
confidently, such as serialization changes, broadly consumed headers, central
build or toolchain logic, and broad refactors. A header or CMake edit with a
small known consumer set is not inherently cross-cutting:

```bash
./scripts/build_and_test.sh              # every non-UI test
./scripts/build_and_test.sh --ui-tests   # only the tests labeled `ui`
./scripts/build_and_test.sh --all-tests-with-ui  # both; what CI runs
```

`--ui-tests` is a filter, not an addition: it runs the `ui` label alone and
skips everything the unflagged command covers. Reach for it when SDL or ImGui
behavior is the thing under test, and for `--all-tests-with-ui` when you want
the merge gate locally.

The comprehensive wrapper uses the `dev-full` or `ui-full` build preset: eight
build workers with link and GoogleTest discovery bounded to a two-worker Ninja
pool. It then derives an authoritative manifest from CTest and runs each test
executable once, serially. This avoids paying process startup for every
discovered GoogleTest case while preserving fixed-directory and display
resource safety. An explicit `--test-filter` still uses CTest so its case-level
selection semantics do not change. Focused `scripts/test.sh` invocations retain
the two-worker `dev` and `ui` presets.

GitHub Actions runs `--all-tests-with-ui` for every pull request and push to
`main`. A new session alone is not a reason to rerun it.

`.clang-tidy` runs the Google checks plus the two readability checks that back
rules above. `scripts/lint.sh` locates LLVM, supplies the macOS SDK when needed,
and refuses an unscoped invocation. A scoped file list runs at most two
translation units concurrently, reports one concise success line per file, and
prints the complete diagnostic output for failures. Pass all edited translation
units in one invocation so setup is shared and the analyses overlap. Successful
scoped results are cached under `build/dev/.lint-cache`; the key includes the
translation unit's compiler-discovered dependencies, compile command, lint
configuration, tool binary, and platform arguments. Use `--no-cache` to force a
fresh analysis. Failures are never cached. `scripts/lint.sh --all` is reserved
for CI and explicit cleanup milestones; it uses two workers by default because
a full analysis is CPU-intensive. Set `LINT_JOBS` deliberately to override the
full-scan limit. Findings it reports are real; this guide still decides what a
rule means when the two disagree.

## Related documents

- [`AGENTS.md`](../AGENTS.md): concise shared agent workflow and engineering
  principles. `CLAUDE.md` imports it instead of duplicating it.
- [`architecture.md`](architecture.md): cross-layer ownership and lifetime.
  Update it when adding or changing an architectural boundary.
- [`roadmap.md`](roadmap.md): what is left to build, including the clang-tidy
  backlog this guide's checks produce.
