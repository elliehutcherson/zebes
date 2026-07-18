# Zebes

A C++ Game Engine project using SDL2, ImGui, and SQLite.

## Documentation

* [Architecture](docs/architecture.md): dependency boundaries, ownership, and
  the texture resource/handle flow.
* [Style Guide](docs/style-guide.md): project conventions layered on the Google
  C++ Style Guide.

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

## Building and Testing

### Quick Start (Sanity Check)

To configure, build, and run all tests in one go, use the provided helper script:

```bash
./scripts/build_and_test.sh
```

The helper uses the `dev` CMake preset. This runs the headless unit and component
tests without requiring a display. You can also run each stage directly:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The SDL/ImGui integration tests require a working display and are kept in a
separate preset:

```bash
./scripts/build_and_test.sh --ui-tests
```

Or run the stages directly with `cmake --preset ui`,
`cmake --build --preset ui`, and `ctest --preset ui`.

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
