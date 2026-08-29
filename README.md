# Zebes

A C++ Game Engine project using SDL2, ImGui, and SQLite.

## Documentation

* [Architecture](docs/architecture.md): dependency boundaries, ownership, and
  the texture resource/handle flow.
* [Style Guide](docs/style-guide.md): project conventions layered on the Google
  C++ Style Guide.
* [Roadmap](docs/roadmap.md): current work, sequencing, and settled decisions.
* [Environment Artwork and Parallax Plan](docs/environment-artwork-plan.md):
  implementation sequence, validation, and the human workflow for layered
  level art.
* [Codex Image Generation](docs/codex-image-generation.md): subscription-backed
  App Server design, implementation status, and remaining integration work.
* [Historical Design Records](docs/history/README.md): completed phases and
  prior handoffs retained for reference.

## Project Structure

* `src/`: Source code for the engine and game logic.
* `tests/`: Unit and integration tests (GoogleTest).
* `assets/`: Game assets (textures, database, configs).
* `include/`: Third-party dependencies (SDL2, Abseil, etc.).

## Dependencies

The project bundles most dependencies in the `include/` directory, managed via CMake:

* **SDL2, SDL2_image, SDL2_ttf**: Windowing and Rendering.
* **Dear ImGui**: UI / Debugging tools.
* **SQLite3**: Database management.
* **GoogleTest**: Testing framework.
* **Abseil**: C++ library augmentations.
* **nlohmann/json**: JSON parsing.

Local builds use the Ninja generator. On macOS, install it with
`brew install ninja`; other platforms may use their package manager's Ninja
package. If the checkout already has a `build/dev`, `build/ui`, or
`build/release` directory configured with the former Unix Makefiles generator,
remove that generated directory once before configuring the corresponding
preset again.

## Building and Testing

### Focused Local Loop

Build and run the affected C++ test executable while developing:

```bash
./scripts/test.sh --list
./scripts/test.sh terrain_generator_test
./scripts/test.sh terrain_generator_test TerrainGeneratorTest.EverySlopeShapeRenders
./scripts/test.sh --ui sanity_test
```

The first command lists the available first-party C++ test targets. A target
without a filter runs the whole test executable; an optional second argument
filters to one GoogleTest case. `--ui` selects the display-dependent preset.
The helper configures the selected preset on first use and then builds only the
requested target.

Lint edited translation units rather than the entire tree:

```bash
./scripts/lint.sh src/terrain/terrain_generator.cc
```

For headers, pass representative `.cc` files that include the changed header.
`./scripts/lint.sh --all` is CPU-intensive and is intended for CI or explicit
cleanup milestones.

### Comprehensive Verification

GitHub Actions runs the full suite on pull requests and pushes to `main`. Run it
locally when a change is cross-cutting or when diagnosing CI:

```bash
./scripts/build_and_test.sh
```

The helper configures the `dev` CMake preset, builds through the eight-worker
`dev-full` build preset, and runs the headless C++ and Python tests. Compile
edges may use all eight workers; link and GoogleTest-discovery edges share a
two-worker pool because higher link concurrency caused discovery timeouts. You
can also run each stage directly:

```bash
cmake --preset dev
cmake --build --preset dev-full
python3 scripts/run_test_executables.py --build-dir build/dev
```

The full runner gets its manifest from CTest, then launches each registered
test executable once instead of launching a process for every discovered
GoogleTest case. `ctest --preset dev` remains available when case-level CTest
reporting is useful. Focused `scripts/test.sh` work continues to use the
two-worker `dev` build preset.

The SDL/ImGui integration tests require a working display and are kept in a
separate preset:

```bash
./scripts/build_and_test.sh --ui-tests
```

Or run the stages directly with `cmake --preset ui`,
`cmake --build --preset ui-full`, and
`python3 scripts/run_test_executables.py --build-dir build/ui --label ui`.

CI uses the same UI-enabled build tree for every C++ test so it compiles the
project only once, then runs the Python suite once. GitHub Actions persists a
ccache compiler cache between runs; a cold run seeds the cache, while later
runs reuse unchanged third-party and project objects:

```bash
./scripts/build_and_test.sh --all-tests-with-ui
```

This comprehensive form is intended for CI; normal local work should continue
to use the focused commands above. A clean UI-enabled build on the reference
16-core macOS machine completed 1,081 actions in 5m49.7s with the eight-worker
build and two-worker link pool. The 96 headless C++ executables take about 12s;
a warm complete UI-enabled wrapper run takes about 37s, including configure,
build checks, 99 C++ executables, and 79 Python tests. The prior case-by-case
CTest C++ run alone took 62–75s for 1,003 processes.

For an optimized editor build without tests:

```bash
cmake --preset release
cmake --build --preset release
```

## Running the Editor

Configure, build, and launch the development editor with:

```bash
./scripts/run_editor.sh
```

Use `--no-build` to launch an existing build or `--release` to build and run
the optimized editor.

## Running the Game

Build and launch the Milestone 1 game runtime with:

```bash
cmake --preset dev
cmake --build --preset dev --target run_game
build/dev/bin/run_game
```

Pass `--unpaced` to advance bounded fixed-step batches as fast as presentation
allows. Real-time mode is the default. In either mode SDL event polling and
presentation stay on the main thread, and config and level assets finish
loading before the runtime loop starts.

## Credentials

The Prop Artwork tab offers two image-generation providers. Codex (ChatGPT) is
selected first and uses the active Codex ChatGPT login plus the enabled
`imagegen` skill; it does not use `OPENAI_API_KEY`. OpenAI API is the explicit
alternative and requires that environment variable.

The editor resolves `codex` from `ZEBES_CODEX_BIN`, then `PATH`, then known
macOS OpenAI editor-extension locations. Set `ZEBES_CODEX_BIN` to an absolute
executable path if a GUI launch cannot inherit the shell path.

The editor does not require either provider. An unavailable provider is
disabled with its reason shown in the Generate source section, while imported
PNG sources and the rest of the editor continue to work.

Copy the template, fill it in, and source it into the shell that launches the
editor:

```bash
cp secrets.env.example secrets.env
set -a; source secrets.env; set +a
./scripts/run_editor.sh
```

`secrets.env` is gitignored. Nothing loads it automatically — credentials reach
the OpenAI API adapter through environment variables only, so a key that is not
sourced is a missing key, never a default.

### The opt-in live provider test

One binary talks to the real provider. It is deliberately unregistered with
CTest and lands outside `bin/tests`, so no automated run can select it, and it
spends real money each time:

```bash
set -a; source secrets.env; set +a
cmake --build --preset dev --target openai_image_client_live_test
build/dev/bin/tools/openai_image_client_live_test
```

Filter to `ComposesTheRealStackWithoutARequest` to check the key and the build
without issuing a generation. Everything deterministic — cancellation, error
mapping, malformed responses — is covered by the fake-driven tests under
`tests/editor/` and has no business here.
