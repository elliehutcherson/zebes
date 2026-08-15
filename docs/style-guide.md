# Zebes Style Guide

Zebes follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
with the project-specific rules below. This document is the human-facing coding
guide; `.clang-format` is the formatting authority.

## Naming

- Types and functions use `PascalCase`.
- Local variables and parameters use `snake_case`.
- Data members use `snake_case_`.
- Constants use `kPascalCase`.
- CMake target and source names use `snake_case`.

## Types and initialization

- Prefer Zebes-owned domain types at library boundaries.
- Do not expose SDL, ImGui, or another dependency's data structures from engine
  or resource interfaces.
- Prefer brace initialization for aggregates and value initialization.
- Use explicit types when they improve readability. Use `auto` when the type is
  cumbersome or already obvious from the expression.
- Prefer references for required dependencies and pointers only for nullable or
  reseatable relationships.
- Express ownership with RAII types such as `std::unique_ptr`.

## Errors and control flow

- Use `absl::Status` and `absl::StatusOr` for recoverable failures.
- Prefer `RETURN_IF_ERROR` and `ASSIGN_OR_RETURN` for propagation.
- Fail early rather than leaving partially valid objects behind.
- Favor guard clauses and small helpers over deeply nested control flow.
- Do not add `else` after a branch that always returns.

## Dependencies and architecture

- Depend on interfaces owned by the layer above an external library.
- Keep SDL implementations under `src/platform/sdl` or SDL-specific UI code.
- Keep ImGui in the editor/UI layer.
- Wire concrete dependencies in the application composition root.
- Do not introduce globals or service locators to avoid normal dependency
  injection.
- Record new cross-layer ownership or lifetime rules in `architecture.md`.

## Serialized formats

- **No optional fields.** Every writer emits every field of the record it is
  writing, and every reader requires it with `.at()`. A definition missing a
  field is corruption, not permission to substitute a default.
- Write collections even when empty. An absent list and an empty one would
  otherwise be two spellings of one state, and the reader would have to guess
  which the author meant.
- **Add a field by adding a migration, not a default.** Extend
  `scripts/migrate_definitions.py` and run it once. A tolerant reader silently
  reinterprets old data forever; a migration moves it once and then the
  invariant holds.
- A record may be a tagged union, and that is not an optional field. When a
  discriminator such as `TerrainScheme` says which variant a record is, each
  variant's fields are required for that variant and absent from the others. The
  reader knows which set to demand before it reads them.
- Read exactly one schema version. Carrying a translation for a version no file
  uses lets absent data decide the parser's shape.
- Every shipped definition is loaded by a test, and every `LoadAll*` reports the
  files it could not read rather than logging and returning success. Strict
  parsing without both is a trap rather than an invariant.

## Headers

- Public headers should document ownership, nullability, lifetime, and error
  behavior when those are not obvious from the type.
- Include what the file uses; do not depend on transitive includes.
- Include required type declarations directly by default. Use forward
  declarations only when they materially reduce coupling or build cost without
  making the header unclear or fragile.
- Comments should explain intent, invariants, and tradeoffs rather than repeat
  the code.

## Testing

- Add deterministic tests for new behavior and failure paths.
- Prefer fakes for stateful platform-neutral interfaces and mocks for verifying
  important interactions.
- Keep engine and resource tests headless.
- Put tests requiring SDL windows or ImGui interaction in the UI test preset.
- A test must fail when required setup or data is missing; do not conditionally
  skip assertions to make it pass.

## Formatting and verification

Format changed C++ files with the repository `.clang-format` configuration.
Before handing off a change, run:

```bash
./scripts/build_and_test.sh
git diff --check
```

Use the UI test workflow when changing behavior that depends on SDL or ImGui:

```bash
./scripts/build_and_test.sh --ui-tests
```
