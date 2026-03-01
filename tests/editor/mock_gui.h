#pragma once

#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "gmock/gmock.h"

namespace zebes {

class MockGui : public GuiInterface {
 public:
  // ImGui Wrappers
  MOCK_METHOD(bool, Begin, (const char* name, bool* p_open, ImGuiWindowFlags flags), (override));
  MOCK_METHOD(void, End, (), (override));
  MOCK_METHOD(bool, BeginListBox, (const char* label, const ImVec2& size), (override));
  MOCK_METHOD(void, EndListBox, (), (override));
  MOCK_METHOD(bool, BeginChild,
              (const char* str_id, const ImVec2& size, bool border, ImGuiWindowFlags flags),
              (override));
  MOCK_METHOD(void, EndChild, (), (override));
  MOCK_METHOD(bool, BeginTabBar, (const char* str_id, ImGuiTabBarFlags flags), (override));
  MOCK_METHOD(void, EndTabBar, (), (override));
  MOCK_METHOD(bool, BeginTabItem, (const char* label, bool* p_open, ImGuiTabItemFlags flags),
              (override));
  MOCK_METHOD(void, EndTabItem, (), (override));
  MOCK_METHOD(bool, BeginTable,
              (const char* str_id, int column, ImGuiTableFlags flags, const ImVec2& outer_size,
               float inner_width),
              (override));
  MOCK_METHOD(void, EndTable, (), (override));
  MOCK_METHOD(void, BeginDisabled, (bool disabled), (override));
  MOCK_METHOD(void, EndDisabled, (), (override));
  MOCK_METHOD(bool, BeginCombo,
              (const char* label, const char* preview_value, ImGuiComboFlags flags), (override));
  MOCK_METHOD(void, EndCombo, (), (override));
  MOCK_METHOD(void, BeginGroup, (), (override));
  MOCK_METHOD(void, EndGroup, (), (override));
  MOCK_METHOD(bool, BeginPopupContextItem,
              (const char* str_id, ImGuiPopupFlags flags), (override));
  MOCK_METHOD(void, EndPopup, (), (override));
  MOCK_METHOD(bool, MenuItem,
              (const char* label, const char* shortcut, bool selected, bool enabled), (override));

  MOCK_METHOD(void, PushID, (const char* str_id), (override));
  MOCK_METHOD(void, PushID, (const char* str_id_begin, const char* str_id_end), (override));
  MOCK_METHOD(void, PushID, (const void* ptr_id), (override));
  MOCK_METHOD(void, PushID, (int int_id), (override));
  MOCK_METHOD(void, PopID, (), (override));

  MOCK_METHOD(void, PushStyleColor, (ImGuiCol idx, ImU32 col), (override));
  MOCK_METHOD(void, PushStyleColor, (ImGuiCol idx, const ImVec4& col), (override));
  MOCK_METHOD(void, PopStyleColor, (int count), (override));

  MOCK_METHOD(void, PushStyleVar, (ImGuiStyleVar idx, float val), (override));
  MOCK_METHOD(void, PushStyleVar, (ImGuiStyleVar idx, const ImVec2& val), (override));
  MOCK_METHOD(void, PopStyleVar, (int count), (override));

  MOCK_METHOD(void, Indent, (float indent_w), (override));
  MOCK_METHOD(void, Unindent, (float indent_w), (override));
  MOCK_METHOD(void, Separator, (), (override));
  MOCK_METHOD(void, SameLine, (float offset_from_start_x, float spacing), (override));
  MOCK_METHOD(void, NewFrame, (), (override));
  MOCK_METHOD(void, AlignTextToFramePadding, (), (override));
  MOCK_METHOD(void, Render, (), (override));

  // Widgets
  MOCK_METHOD(bool, Button, (const char* label, const ImVec2& size), (override));
  MOCK_METHOD(bool, InvisibleButton,
              (const char* str_id, const ImVec2& size, ImGuiButtonFlags flags), (override));
  MOCK_METHOD(bool, ArrowButton, (const char* str_id, ImGuiDir dir), (override));
  // Variadic functions cannot be mocked directly with MOCK_METHOD.
  // We'll need to define them and delegate to a non-variadic mock or just mock the most common
  // usages if strict verification is needed. For now, let's provide a basic implementation that
  // does nothing or a minimal mock. Ideally we should use a proxy mock or `MOCK_METHOD` with
  // `DoText` if we change the interface. But since we can't change the interface easily right now,
  // checking how `GuiInterface` defines them. They are virtual. Creating a "Do..." helper is best
  // practice but requires interface change. Given I am implementing `MockGui` for strict
  // `GuiInterface`, I will have to implement the variadic functions to satisfy the linker.

  void Text(const char* fmt, ...) override {}
  void TextColored(const ImVec4& col, const char* fmt, ...) override {}
  void TextDisabled(const char* fmt, ...) override {}
  void TextWrapped(const char* fmt, ...) override {}
  void LabelText(const char* label, const char* fmt, ...) override {}

  MOCK_METHOD(bool, Checkbox, (const char* label, bool* v), (override));
  MOCK_METHOD(bool, SliderInt,
              (const char* label, int* v, int v_min, int v_max, const char* format,
               ImGuiSliderFlags flags),
              (override));
  MOCK_METHOD(bool, InputText,
              (const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags,
               ImGuiInputTextCallback callback, void* user_data),
              (override));
  MOCK_METHOD(bool, InputText,
              (const char* label, std::string* str, ImGuiInputTextFlags flags,
               ImGuiInputTextCallback callback, void* user_data),
              (override));
  MOCK_METHOD(bool, InputInt,
              (const char* label, int* v, int step, int step_fast, ImGuiInputTextFlags flags),
              (override));
  MOCK_METHOD(bool, InputDouble,
              (const char* label, double* v, double step, double step_fast, const char* format,
               ImGuiInputTextFlags flags),
              (override));
  MOCK_METHOD(bool, InputFloat,
              (const char* label, float* v, float step, float step_fast, const char* format,
               ImGuiInputTextFlags flags),
              (override));

  MOCK_METHOD(bool, Selectable,
              (const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size),
              (override));
  MOCK_METHOD(bool, Selectable,
              (const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size),
              (override));

  MOCK_METHOD(void, Image,
              (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0,
               const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col),
              (override));
  MOCK_METHOD(void, Dummy, (const ImVec2& size), (override));
  MOCK_METHOD(void, Spacing, (), (override));

  // Tables
  MOCK_METHOD(void, TableSetupColumn,
              (const char* label, ImGuiTableColumnFlags flags, float init_width_or_weight,
               ImGuiID user_id),
              (override));
  MOCK_METHOD(void, TableHeadersRow, (), (override));
  MOCK_METHOD(void, TableNextRow, (ImGuiTableRowFlags row_flags, float min_row_height), (override));
  MOCK_METHOD(bool, TableNextColumn, (), (override));

  // Layout
  MOCK_METHOD(void, SetCursorPos, (const ImVec2& local_pos), (override));
  MOCK_METHOD(void, SetCursorPosX, (float local_x), (override));
  MOCK_METHOD(void, SetCursorScreenPos, (const ImVec2& pos), (override));
  MOCK_METHOD(ImVec2, GetCursorPos, (), (const, override));
  MOCK_METHOD(float, GetCursorPosX, (), (const, override));
  MOCK_METHOD(float, GetCursorPosY, (), (const, override));
  MOCK_METHOD(ImVec2, GetCursorScreenPos, (), (const, override));
  MOCK_METHOD(void, SetNextWindowPos, (const ImVec2& pos, ImGuiCond cond, const ImVec2& pivot),
              (override));
  MOCK_METHOD(void, SetNextWindowSize, (const ImVec2& size, ImGuiCond cond), (override));

  MOCK_METHOD(void, PushItemWidth, (float item_width), (override));
  MOCK_METHOD(void, PopItemWidth, (), (override));
  MOCK_METHOD(void, SetNextItemWidth, (float item_width), (override));
  MOCK_METHOD(float, GetTextLineHeightWithSpacing, (), (const, override));
  MOCK_METHOD(ImVec2, GetContentRegionAvail, (), (const, override));
  MOCK_METHOD(ImDrawList*, GetWindowDrawList, (), (override));
  MOCK_METHOD(ImVec2, GetMousePos, (), (const, override));
  MOCK_METHOD(ImGuiIO&, GetIO, (), (override));
  MOCK_METHOD(ImGuiStyle&, GetStyle, (), (override));
  MOCK_METHOD(bool, IsItemHovered, (ImGuiHoveredFlags flags), (override));
  MOCK_METHOD(void, SetItemDefaultFocus, (), (override));
  MOCK_METHOD(bool, IsItemActive, (), (override));
  MOCK_METHOD(bool, IsItemClicked, (ImGuiMouseButton mouse_button), (override));
  MOCK_METHOD(bool, IsItemDeactivatedAfterEdit, (), (override));
  MOCK_METHOD(bool, IsMouseDragging, (ImGuiMouseButton button, float lock_threshold), (override));
  MOCK_METHOD(ImVec2, GetWindowSize, (), (const, override));
  MOCK_METHOD(bool, IsWindowHovered, (ImGuiHoveredFlags flags), (override));
  MOCK_METHOD(bool, IsWindowFocused, (ImGuiFocusedFlags flags), (override));

  MOCK_METHOD(bool, CollapsingHeader, (const char* label, ImGuiTreeNodeFlags flags), (override));
  MOCK_METHOD(bool, TreeNodeEx, (const char* label, ImGuiTreeNodeFlags flags), (override));

  // Scoped Object Creation
  MOCK_METHOD(ImGuiViewport*, GetMainViewport, (), (override));
  MOCK_METHOD(bool, IsKeyDown, (ImGuiKey key), (override));
  MOCK_METHOD(bool, IsKeyPressed, (ImGuiKey key, bool repeat), (override));
  MOCK_METHOD(void, ShowMetricsWindow, (bool* p_open), (override));

  MOCK_METHOD(ScopedListBox, CreateScopedListBox, (const char* label, ImVec2 size), (override));
  MOCK_METHOD(ScopedChild, CreateScopedChild,
              (const char* str_id, ImVec2 size, bool border, ImGuiWindowFlags flags), (override));
  MOCK_METHOD(ScopedTabBar, CreateScopedTabBar, (const char* str_id, ImGuiTabBarFlags flags),
              (override));
  MOCK_METHOD(ScopedTabItem, CreateScopedTabItem,
              (const char* label, bool* p_open, ImGuiTabItemFlags flags), (override));
  MOCK_METHOD(ScopedTable, CreateScopedTable,
              (const char* str_id, int column, ImGuiTableFlags flags, const ImVec2& outer_size,
               float inner_width),
              (override));
  MOCK_METHOD(ScopedDisabled, CreateScopedDisabled, (bool disabled), (override));
  MOCK_METHOD(ScopedWindow, CreateScopedWindow,
              (const char* name, bool* p_open, ImGuiWindowFlags flags), (override));
  MOCK_METHOD(ScopedCombo, CreateScopedCombo,
              (const char* label, const char* preview_value, ImGuiComboFlags flags), (override));
  MOCK_METHOD(ScopedGroup, CreateScopedGroup, (), (override));
  MOCK_METHOD(ScopedId, CreateScopedId, (const char* str_id), (override));
  MOCK_METHOD(ScopedId, CreateScopedId, (const char* str_id_begin, const char* str_id_end),
              (override));
  MOCK_METHOD(ScopedId, CreateScopedId, (const void* ptr_id), (override));
  MOCK_METHOD(ScopedId, CreateScopedId, (int int_id), (override));
  MOCK_METHOD(ScopedStyleColor, CreateScopedStyleColor, (ImGuiCol idx, ImU32 col), (override));
  MOCK_METHOD(ScopedStyleColor, CreateScopedStyleColor, (ImGuiCol idx, const ImVec4& col),
              (override));
  MOCK_METHOD(ScopedStyleVar, CreateScopedStyleVar, (ImGuiStyleVar idx, float val), (override));
  MOCK_METHOD(ScopedStyleVar, CreateScopedStyleVar, (ImGuiStyleVar idx, const ImVec2& val),
              (override));
  MOCK_METHOD(ScopedPopup, CreateScopedPopupContextItem,
              (const char* str_id, ImGuiPopupFlags flags), (override));
};

}  // namespace zebes
