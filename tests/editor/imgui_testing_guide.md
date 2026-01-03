# ImGui Testing Guide

This guide summarizes learnings and patterns for testing ImGui windows and widgets using the `imgui_test_engine`, specifically within the `tests/editor` context.

## 1. Setup and Registration

Tests are registered using `IM_REGISTER_TEST`. Ensure the test engine context is properly initialized before running checks.

```cpp
IM_REGISTER_TEST(engine, "category", "test_name")->TestFunc = [](ImGuiTestContext* ctx) {
    // ...
};
```

## 2. Finding Windows

ImGui windows often have generated suffixes in their names (e.g., `WindowName_00FAB123`) or paths (e.g., `Parent/Child_123`).
**Do NOT** rely on exact string matches for window names if they are dynamically created or docked.

### Robust Window Finding Pattern
Iterate through `ctx->UiContext->Windows` and check for substring matches.

```cpp
ImGuiWindow* target_window = nullptr;
for (int i = 0; i < ctx->UiContext->Windows.Size; ++i) {
    ImGuiWindow* w = ctx->UiContext->Windows[i];
    if (strstr(w->Name, "Parent/TargetWindow") != nullptr) {
        target_window = w;
        break;
    }
}
IM_CHECK(target_window != nullptr);
ctx->SetRef(target_window);
```

## 3. Coordinate Systems

Understanding coordinate spaces is critical for inputs like clicking and dragging.

*   **Screen Coordinates**: Absolute pixels on the screen/viewport. `ctx->MouseMoveToPos` typically expects these.
*   **Window Coordinates**: Relative to the window top-left.
*   **World Coordinates**: Your application's internal coordinate system (e.g., Canvas abstraction).

### Precision Input
When interacting with custom widgets (like a Canvas using `InvisibleButton`):
1.  **Do not guess offsets.** Padding and borders vary.
2.  **Use the Application's helper methods.** If your `Canvas` class has `WorldToScreen`, expose it to the test harness and use it.

```cpp
// Bad: Guessing padding
ImVec2 target = center + ImVec2(10, 10); // Might be off by border size (e.g. 16px)

// Good: Using App Source of Truth
ImVec2 target = app.canvas()->WorldToScreen(Vec(10, 10));
```

## 4. Handling Invisible Inputs

If a widget uses `ImGui::InvisibleButton` to capture input:
*   Use `ctx->ItemInfo("##ButtonID")` to find it.
*   The item's `RectFull` gives its screen bounds.
*   Ensure you have `SetRef` to the correct parent window, otherwise `ItemInfo` may fail to find the ID.

## 5. Debugging Tips

If a test fails or can't find an item:
1.  **Print Window Names**: Dump the list of active windows to see exactly what ImGui sees.
    ```cpp
    for (int i = 0; i < ctx->UiContext->Windows.Size; ++i) {
        fprintf(stderr, "Window: %s\n", ctx->UiContext->Windows[i]->Name);
    }
    ```
2.  **Check Input State**: Use `IsMouseClicked` debug prints in your application code (temporarily) to verify if inputs are reaching the logic.
3.  **Trace Coordinates**: Print expected vs actual screen coordinates to detect offset issues.

## 6. Common Pitfalls
*   **Frame Lag**: Use `ctx->Yield(2)` after opening windows or changing state to ensure layout passes have run and `p0` (cursor start pos) is stable.
*   **Ambiguous Drags**: If valid drag handles are small (e.g., vertices), ensure your click target is precise (within the hit threshold).
*   **Window Context**: If your test renders components that require a window (like `BeginTable`, `BeginChild`), ensure your `AppDraw` callback wraps the render call in `ImGui::Begin()` / `ImGui::End()`. The test engine runner does NOT automatically provide a window for your app code.
*   **Missing Resources**: Tests often run without real assets (textures, fonts). Ensure your UI code handles null pointers (e.g. missing `SDL_Texture*`) gracefully. If a widget depends on a texture to render itself, consider adding a fallback visualization (e.g. a placeholder rectangle) so that the widget is still interactable and testable.
*   **Fail Fast**: Use `LOG(FATAL)` or `CHECK` macros for invariants that should never be violated (like null pointers for required resources). This makes bugs easier to catch than silent returns.

