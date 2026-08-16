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

<!-- rule:cpp-style paths="**/*.cc,**/*.h,**/CMakeLists.txt,**/*.cmake" -->

## C++

The [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html),
plus the rules below. `.clang-format` is the formatting authority; a hook runs it
on files Claude edits, and your own edits should go through your editor's
clang-format integration or `clang-format -i`.

### Naming

- Types and functions: `PascalCase`
- Locals and parameters: `snake_case`
- Data members: `snake_case_`
- Constants: `kPascalCase`
- CMake targets and source file names: `snake_case`

### Types and initialization

- Brace-initialize aggregates and PODs.
- Use explicit types. Use `auto` only for cumbersome types (iterators) or when
  the expression already names the type (`std::make_unique<Foo>()`).
- References for required dependencies. Pointers only for nullable or
  reseatable ones. Never null-check a reference.
- Express ownership with RAII types such as `std::unique_ptr`.
- Mark a member function `static` when it does not read or modify instance
  state. Nothing enforces this mechanically; the linter check for it is off.
- Prefer Zebes-owned domain types at library boundaries. Do not expose SDL or
  ImGui types from engine or resource interfaces.

### Libraries

Use `absl::Status`, `absl::StatusOr`, `absl::flat_hash_map`, `absl::StrFormat`
over their STL equivalents.

### Errors

- `absl::Status` and `absl::StatusOr` for recoverable failures.
- `RETURN_IF_ERROR` and `ASSIGN_OR_RETURN` to propagate.
- Fail immediately. Never return a partially constructed object or fall back to
  a default state.

### Control flow

- Guard clauses and early returns over `if`/`else`.
- No `else` after a branch that returns.
- More than two levels of indentation means extract a private helper.

### Headers

- Include what the file uses. Do not rely on transitive includes.
- Include type declarations directly by default. Forward declare only when it
  materially cuts coupling or build time.
- Document ownership, nullability, lifetime, and error behavior when the type
  does not make them obvious.
- Comment intent, invariants, and tradeoffs. Never restate the code.

### Layering

- Depend on interfaces owned by the layer above an external library.
- SDL implementations live under `src/platform/sdl` or SDL-specific UI code.
- ImGui stays in the editor/UI layer.
- Wire concrete dependencies in the application composition root. No globals,
  no service locators.
- Record new cross-layer ownership or lifetime rules in `docs/architecture.md`.

<!-- /rule -->

<!-- rule:definitions paths="src/resources/**,src/objects/**,scripts/migrate_definitions.py,tests/resources/**,tests/assets/**" -->

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

<!-- rule:testing paths="tests/**" -->

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
ImGui interaction belongs in the UI test preset, run with:

```bash
./scripts/build_and_test.sh --ui-tests
```

If a test needs a window, that usually means the code under test reaches too
far down the stack. Check whether the dependency can be an interface first.

### Definitions

Every shipped definition file is loaded by a test. Adding a definition means
adding it to that test.

<!-- /rule -->

---

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

Before handing off a change:

```bash
./scripts/build_and_test.sh              # build + headless tests
./scripts/build_and_test.sh --ui-tests   # when the change touches SDL or ImGui
git diff --check                         # trailing whitespace, conflict markers
```

`.clang-tidy` runs the Google checks plus the two readability checks that back
rules above. It is not wired into the build, so run it by hand:

```bash
PATH="/usr/local/opt/llvm/bin:$PATH" run-clang-tidy -p build/dev -quiet -j 8 \
    -extra-arg=-isysroot -extra-arg="$(xcrun --show-sdk-path)" src/
```

clang-tidy ships with the keg-only Homebrew llvm formula, which is why it is not
already on `PATH`. The `-isysroot` pair is required and `.clang-tidy` explains
why. Findings it reports are real; this guide still decides what a rule means
when the two disagree.

## Related documents

- [`CLAUDE.md`](../CLAUDE.md): build commands, the debugging protocol, and the
  escalation rule. Loaded into every Claude Code session.
- [`architecture.md`](architecture.md): cross-layer ownership and lifetime.
  Update it when adding or changing an architectural boundary.
- [`roadmap.md`](roadmap.md): what is left to build, including the clang-tidy
  backlog this guide's checks produce.
