#pragma once

#include <string>

#include "imgui.h"

namespace zebes {

// Forward declarations for scoped wrappers
class ScopedListBox;
class ScopedChild;
class ScopedTabBar;
class ScopedTabItem;
class ScopedTable;
class ScopedDisabled;
class ScopedWindow;
class ScopedCombo;
class ScopedGroup;
class ScopedId;
class ScopedStyleColor;
class ScopedStyleVar;

class GuiInterface {
 public:
  virtual ~GuiInterface() = default;

  // ImGui Wrappers
  virtual bool Begin(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0) = 0;
  virtual void End() = 0;
  virtual bool BeginListBox(const char* label, const ImVec2& size = ImVec2(0, 0)) = 0;
  virtual void EndListBox() = 0;
  virtual bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0, 0),
                          bool border = false, ImGuiWindowFlags flags = 0) = 0;
  virtual void EndChild() = 0;
  virtual bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags = 0) = 0;
  virtual void EndTabBar() = 0;
  virtual bool BeginTabItem(const char* label, bool* p_open = nullptr,
                            ImGuiTabItemFlags flags = 0) = 0;
  virtual void EndTabItem() = 0;
  virtual bool BeginTable(const char* str_id, int column, ImGuiTableFlags flags = 0,
                          const ImVec2& outer_size = ImVec2(0.0f, 0.0f),
                          float inner_width = 0.0f) = 0;
  virtual void EndTable() = 0;
  virtual void BeginDisabled(bool disabled = true) = 0;
  virtual void EndDisabled() = 0;
  virtual bool BeginCombo(const char* label, const char* preview_value,
                          ImGuiComboFlags flags = 0) = 0;
  virtual void EndCombo() = 0;
  virtual void BeginGroup() = 0;
  virtual void EndGroup() = 0;

  virtual void PushID(const char* str_id) = 0;
  virtual void PushID(const char* str_id_begin, const char* str_id_end) = 0;
  virtual void PushID(const void* ptr_id) = 0;
  virtual void PushID(int int_id) = 0;
  virtual void PopID() = 0;

  virtual void PushStyleColor(ImGuiCol idx, ImU32 col) = 0;
  virtual void PushStyleColor(ImGuiCol idx, const ImVec4& col) = 0;
  virtual void PopStyleColor(int count = 1) = 0;

  virtual void PushStyleVar(ImGuiStyleVar idx, float val) = 0;
  virtual void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val) = 0;
  virtual void PopStyleVar(int count = 1) = 0;

  virtual void Indent(float indent_w = 0.0f) = 0;
  virtual void Unindent(float indent_w = 0.0f) = 0;
  virtual void Separator() = 0;
  virtual void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f) = 0;
  virtual void NewFrame() = 0;
  virtual void AlignTextToFramePadding() = 0;
  virtual void Render() = 0;

  // Widgets
  virtual bool Button(const char* label, const ImVec2& size = ImVec2(0, 0)) = 0;
  virtual bool InvisibleButton(const char* str_id, const ImVec2& size,
                               ImGuiButtonFlags flags = 0) = 0;
  virtual bool ArrowButton(const char* str_id, ImGuiDir dir) = 0;
  virtual void Text(const char* fmt, ...) = 0;
  virtual void TextColored(const ImVec4& col, const char* fmt, ...) = 0;
  virtual void TextDisabled(const char* fmt, ...) = 0;
  virtual void TextWrapped(const char* fmt, ...) = 0;
  virtual void LabelText(const char* label, const char* fmt, ...) = 0;

  virtual bool Checkbox(const char* label, bool* v) = 0;
  virtual bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d",
                         ImGuiSliderFlags flags = 0) = 0;
  virtual bool InputText(const char* label, char* buf, size_t buf_size,
                         ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr,
                         void* user_data = nullptr) = 0;
  virtual bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags = 0,
                         ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr) = 0;
  virtual bool InputInt(const char* label, int* v, int step = 1, int step_fast = 100,
                        ImGuiInputTextFlags flags = 0) = 0;
  virtual bool InputDouble(const char* label, double* v, double step = 0.0, double step_fast = 0.0,
                           const char* format = "%.6f", ImGuiInputTextFlags flags = 0) = 0;

  virtual bool Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0,
                          const ImVec2& size = ImVec2(0, 0)) = 0;
  virtual bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags = 0,
                          const ImVec2& size = ImVec2(0, 0)) = 0;

  virtual void Image(ImTextureID user_texture_id, const ImVec2& size,
                     const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1),
                     const ImVec4& tint_col = ImVec4(1, 1, 1, 1),
                     const ImVec4& border_col = ImVec4(0, 0, 0, 0)) = 0;
  virtual void Dummy(const ImVec2& size) = 0;
  virtual void Spacing() = 0;

  // Tables
  virtual void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags = 0,
                                float init_width_or_weight = 0.0f, ImGuiID user_id = 0) = 0;
  virtual void TableHeadersRow() = 0;
  virtual void TableNextRow(ImGuiTableRowFlags row_flags = 0, float min_row_height = 0.0f) = 0;
  virtual bool TableNextColumn() = 0;

  // Layout
  virtual void SetCursorPos(const ImVec2& local_pos) = 0;
  virtual void SetCursorPosX(float local_x) = 0;
  virtual void SetCursorScreenPos(const ImVec2& pos) = 0;
  virtual ImVec2 GetCursorPos() const = 0;
  virtual float GetCursorPosX() const = 0;
  virtual float GetCursorPosY() const = 0;
  virtual ImVec2 GetCursorScreenPos() const = 0;
  virtual void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond = 0,
                                const ImVec2& pivot = ImVec2(0, 0)) = 0;
  virtual void SetNextWindowSize(const ImVec2& size, ImGuiCond cond = 0) = 0;

  virtual void PushItemWidth(float item_width) = 0;
  virtual void PopItemWidth() = 0;
  virtual void SetNextItemWidth(float item_width) = 0;
  virtual float GetTextLineHeightWithSpacing() const = 0;
  virtual ImVec2 GetContentRegionAvail() const = 0;
  virtual ImDrawList* GetWindowDrawList() = 0;
  virtual ImVec2 GetMousePos() const = 0;
  virtual ImGuiIO& GetIO() = 0;
  virtual ImGuiStyle& GetStyle() = 0;
  virtual bool IsItemHovered(ImGuiHoveredFlags flags = 0) = 0;
  virtual void SetItemDefaultFocus() = 0;
  virtual bool IsItemActive() = 0;

  virtual bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) = 0;

  // Scoped Object Creation
  virtual ImGuiViewport* GetMainViewport() = 0;
  virtual bool IsKeyPressed(ImGuiKey key, bool repeat = true) = 0;
  virtual void ShowMetricsWindow(bool* p_open = nullptr) = 0;

  virtual ScopedListBox CreateScopedListBox(const char* label, ImVec2 size = ImVec2(0, 0)) = 0;
  virtual ScopedChild CreateScopedChild(const char* str_id, ImVec2 size = ImVec2(0, 0),
                                        bool border = false, ImGuiWindowFlags flags = 0) = 0;
  virtual ScopedTabBar CreateScopedTabBar(const char* str_id, ImGuiTabBarFlags flags = 0) = 0;
  virtual ScopedTabItem CreateScopedTabItem(const char* label, bool* p_open = nullptr,
                                            ImGuiTabItemFlags flags = 0) = 0;
  virtual ScopedTable CreateScopedTable(const char* str_id, int column, ImGuiTableFlags flags = 0,
                                        const ImVec2& outer_size = ImVec2(0.0f, 0.0f),
                                        float inner_width = 0.0f) = 0;
  virtual ScopedDisabled CreateScopedDisabled(bool disabled = true) = 0;
  virtual ScopedWindow CreateScopedWindow(const char* name, bool* p_open = nullptr,
                                          ImGuiWindowFlags flags = 0) = 0;
  virtual ScopedCombo CreateScopedCombo(const char* label, const char* preview_value,
                                        ImGuiComboFlags flags = 0) = 0;
  virtual ScopedGroup CreateScopedGroup() = 0;
  virtual ScopedId CreateScopedId(const char* str_id) = 0;
  virtual ScopedId CreateScopedId(const char* str_id_begin, const char* str_id_end) = 0;
  virtual ScopedId CreateScopedId(const void* ptr_id) = 0;
  virtual ScopedId CreateScopedId(int int_id) = 0;
  virtual ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, ImU32 col) = 0;
  virtual ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, const ImVec4& col) = 0;
  virtual ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, float val) = 0;
  virtual ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, const ImVec2& val) = 0;
};

}  // namespace zebes
