---
paths:
  - "tests/**"
---

# Tests

## Determinism

Tests must be deterministic. If something is expected to exist, assert that it
exists and fail when it does not.

Never guard an assertion with an `if` to make a test pass. Never skip a case
because setup or data is missing — a missing fixture is a failure, not a
reason to pass silently.

Add tests for the failure paths, not only the success path. Every
`absl::Status` a function can return should have a test that produces it.

## Fakes and mocks

- Fakes for stateful, platform-neutral interfaces.
- Mocks only to verify an interaction that matters. Do not mock a type just to
  hand it to a constructor; use the real type or a fake.

## Headless by default

Engine and resource tests run headless. A test that needs an SDL window or
ImGui interaction belongs in the UI test preset, run with:

```bash
./scripts/build_and_test.sh --ui-tests
```

If a test needs a window, that usually means the code under test reaches too
far down the stack. Check whether the dependency can be an interface first.

## Definitions

Every shipped definition file is loaded by a test. Adding a definition means
adding it to that test.