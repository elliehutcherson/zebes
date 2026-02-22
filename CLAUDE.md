# C++ Engineering Rules: Zebes Engine

## 1. Task Execution Protocol

* **Build System:** Use **CMake** exclusively (do not use Bazel).
* **Sanity Check:** Before starting any task, run `scripts/build_and_test.sh` to ensure a clean state.
* **Verification:** Finalize every task by confirming all CMake targets compile and all tests pass via `scripts/build_and_test.sh`.

## 2. Testing & Debugging Protocol

* **Determinism:** Tests must be deterministic. If an item is expected to exists, check that it does exist an fail the test if it does not. Do not guard test logic with if statements just to make them pass.
* **No Shotgun Debugging:** Do not randomly permute syntax (e.g., changing brace initialization to constructor calls, reordering fields) hoping for a fix.
* **Analyze First:** Critically read failure logs and test output.
* **Instrument, Don't Mutate:** If the root cause is unclear, add debug logs (`LOG(INFO)`) or counters to trace execution flow before changing logic.
* **Escalation:** If you cannot identify the cause through analysis and instrumentation, **STOP**. Do not guess. Ask: *"How is this intended to work?"* or *"I am stuck on this test failure."*

## 3. Coding Standards (Google C++ Style)

* **Naming Conventions:**
  * **Variables:** Use `snake_case` for local variables.
  * **Class Members:** Use `snake_case_` (with a trailing underscore).
  * **Functions:** Use `PascalCase()`.
  * **Types (Classes/Structs):** Use `PascalCase`.
  * **Const Expressions:** Use `kPascalCase`.
* **Initialization:**
  * Prefer brace initialization (`{}`) for PODs and aggregates to ensure zero-initialization and prevent narrowing conversions.
* **Type Inference (`auto`):**
  * Avoid `auto` unless the type is cumbersome (e.g., complex iterators) or redundant (e.g., `auto p = std::make_unique<Type>();`).
  * Prefer explicit types for readability in all other cases.
* **Style Enforcement:** Strictly adhere to the Google C++ Style Guide. Use `clang-format` to verify.
* **Libraries:** Prioritize **Abseil (`absl`)** types and utilities (e.g., `absl::Status`, `absl::flat_hash_map`, `absl::StrFormat`) over STL equivalents where Abseil offers better safety or performance.

## 4. Architecture & Safety

* **RAII:** Enforce RAII. Assume resources are initialized on construction.
* **Pointers vs. References:** Prefer references (`T&` or `const T&`). Use pointers only when nullability is required. No redundant null checks for references.
* **Statelessness:** Mark functions `static` if they do not modify instance state.
* **Error Handling (Fail Fast):**
  * **Philosophy:** Never fallback to undefined states or attempt partial recovery. Return errors immediately.
  * **Macros:** Liberally use `RETURN_IF_ERROR` and `ASSIGN_OR_RETURN` when dealing with `absl::Status` or `absl::StatusOr`.
* **Control Flow & Structure:**
  * **Nesting:** Strictly reduce nesting. If logic requires more than 2 levels of indentation, extract it into a private helper function.
  * **Guard Clauses:** Always favor early returns (`return;` or returning status) over `if/else` blocks.
  * **No `else` after `return`:** Keep logic branches flat and linear.

## 5. Documentation

* **Public Interfaces:** Provide thorough comments in headers.
* **Complexity:** Comment "the why" for non-obvious logic. Avoid "the what" for self-documenting code.
