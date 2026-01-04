# Zebes

A C++ Game Engine project using SDL2, ImGui, and SQLite.

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
./scripts/build_and_test.sha
```
