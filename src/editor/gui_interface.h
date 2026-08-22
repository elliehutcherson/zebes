#pragma once

#include <optional>
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
class ScopedPopup;

class GuiInterface {
 public:
  virtual ~GuiInterface() = default;

  bool Begin(const char* name) { return Begin(name, nullptr, 0); }
  bool Begin(const char* name, bool* p_open) { return Begin(name, p_open, 0); }
  virtual bool Begin(const char* name, bool* p_open, ImGuiWindowFlags flags) = 0;
  virtual void End() = 0;
  bool BeginListBox(const char* label) { return BeginListBox(label, ImVec2(0, 0)); }
  virtual bool BeginListBox(const char* label, const ImVec2& size) = 0;
  virtual void EndListBox() = 0;
  bool BeginChild(const char* str_id) { return BeginChild(str_id, ImVec2(0, 0), false, 0); }
  bool BeginChild(const char* str_id, const ImVec2& size) {
    return BeginChild(str_id, size, false, 0);
  }
  bool BeginChild(const char* str_id, const ImVec2& size, bool border) {
    return BeginChild(str_id, size, border, 0);
  }
  virtual bool BeginChild(const char* str_id, const ImVec2& size, bool border,
                          ImGuiWindowFlags flags) = 0;
  virtual void EndChild() = 0;
  bool BeginTabBar(const char* str_id) { return BeginTabBar(str_id, 0); }
  virtual bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags) = 0;
  virtual void EndTabBar() = 0;
  bool BeginTabItem(const char* label) { return BeginTabItem(label, nullptr, 0); }
  bool BeginTabItem(const char* label, bool* p_open) { return BeginTabItem(label, p_open, 0); }
  virtual bool BeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags) = 0;
  virtual void EndTabItem() = 0;
  bool BeginTable(const char* str_id, int column) {
    return BeginTable(str_id, column, 0, ImVec2(0.0f, 0.0f), 0.0f);
  }
  bool BeginTable(const char* str_id, int column, ImGuiTableFlags flags) {
    return BeginTable(str_id, column, flags, ImVec2(0.0f, 0.0f), 0.0f);
  }
  bool BeginTable(const char* str_id, int column, ImGuiTableFlags flags, const ImVec2& outer_size) {
    return BeginTable(str_id, column, flags, outer_size, 0.0f);
  }
  virtual bool BeginTable(const char* str_id, int column, ImGuiTableFlags flags,
                          const ImVec2& outer_size, float inner_width) = 0;
  virtual void EndTable() = 0;
  void BeginDisabled() { BeginDisabled(true); }
  virtual void BeginDisabled(bool disabled) = 0;
  virtual void EndDisabled() = 0;
  bool BeginCombo(const char* label, const char* preview_value) {
    return BeginCombo(label, preview_value, 0);
  }
  virtual bool BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags) = 0;
  virtual void EndCombo() = 0;
  virtual void BeginGroup() = 0;
  virtual void EndGroup() = 0;
  // Opens a context popup when the user right-clicks the previously rendered item.
  // EndPopup() must be called only if this returns true; prefer CreateScopedPopupContextItem.
  bool BeginPopupContextItem() { return BeginPopupContextItem(nullptr, 0); }
  bool BeginPopupContextItem(const char* str_id) { return BeginPopupContextItem(str_id, 0); }
  virtual bool BeginPopupContextItem(const char* str_id, ImGuiPopupFlags flags) = 0;
  virtual void EndPopup() = 0;
  bool MenuItem(const char* label) { return MenuItem(label, nullptr, false, true); }
  bool MenuItem(const char* label, const char* shortcut) {
    return MenuItem(label, shortcut, false, true);
  }
  bool MenuItem(const char* label, const char* shortcut, bool selected) {
    return MenuItem(label, shortcut, selected, true);
  }
  virtual bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled) = 0;

  virtual void PushID(const char* str_id) = 0;
  virtual void PushID(const char* str_id_begin, const char* str_id_end) = 0;
  virtual void PushID(const void* ptr_id) = 0;
  virtual void PushID(int int_id) = 0;
  virtual void PopID() = 0;

  virtual void PushStyleColor(ImGuiCol idx, ImU32 col) = 0;
  virtual void PushStyleColor(ImGuiCol idx, const ImVec4& col) = 0;
  void PopStyleColor() { PopStyleColor(1); }
  virtual void PopStyleColor(int count) = 0;

  virtual void PushStyleVar(ImGuiStyleVar idx, float val) = 0;
  virtual void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val) = 0;
  void PopStyleVar() { PopStyleVar(1); }
  virtual void PopStyleVar(int count) = 0;

  void Indent() { Indent(0.0f); }
  virtual void Indent(float indent_w) = 0;
  void Unindent() { Unindent(0.0f); }
  virtual void Unindent(float indent_w) = 0;
  virtual void Separator() = 0;
  void SameLine() { SameLine(0.0f, -1.0f); }
  void SameLine(float offset_from_start_x) { SameLine(offset_from_start_x, -1.0f); }
  virtual void SameLine(float offset_from_start_x, float spacing) = 0;
  virtual void NewFrame() = 0;
  virtual void AlignTextToFramePadding() = 0;
  virtual void Render() = 0;

  bool Button(const char* label) { return Button(label, ImVec2(0, 0)); }
  virtual bool Button(const char* label, const ImVec2& size) = 0;
  bool InvisibleButton(const char* str_id, const ImVec2& size) {
    return InvisibleButton(str_id, size, 0);
  }
  virtual bool InvisibleButton(const char* str_id, const ImVec2& size, ImGuiButtonFlags flags) = 0;
  virtual bool ArrowButton(const char* str_id, ImGuiDir dir) = 0;
  virtual void Text(const char* fmt, ...) = 0;
  virtual void TextColored(const ImVec4& col, const char* fmt, ...) = 0;
  virtual void TextDisabled(const char* fmt, ...) = 0;
  virtual void TextWrapped(const char* fmt, ...) = 0;
  virtual void LabelText(const char* label, const char* fmt, ...) = 0;

  virtual bool Checkbox(const char* label, bool* v) = 0;
  bool SliderFloat(const char* label, float* v, float v_min, float v_max) {
    return SliderFloat(label, v, v_min, v_max, "%.3f", 0);
  }
  bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format) {
    return SliderFloat(label, v, v_min, v_max, format, 0);
  }
  virtual bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                           const char* format, ImGuiSliderFlags flags) = 0;
  bool SliderInt(const char* label, int* v, int v_min, int v_max) {
    return SliderInt(label, v, v_min, v_max, "%d", 0);
  }
  bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format) {
    return SliderInt(label, v, v_min, v_max, format, 0);
  }
  virtual bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format,
                         ImGuiSliderFlags flags) = 0;
  // Edits three floats in [0,1] as a colour swatch. col must point at three
  // contiguous floats.
  bool ColorEdit3(const char* label, float col[3]) { return ColorEdit3(label, col, 0); }
  virtual bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags) = 0;
  bool InputText(const char* label, char* buf, size_t buf_size) {
    return InputText(label, buf, buf_size, 0, nullptr, nullptr);
  }
  bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags) {
    return InputText(label, buf, buf_size, flags, nullptr, nullptr);
  }
  bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags,
                 ImGuiInputTextCallback callback) {
    return InputText(label, buf, buf_size, flags, callback, nullptr);
  }
  virtual bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags,
                         ImGuiInputTextCallback callback, void* user_data) = 0;
  bool InputText(const char* label, std::string* str) {
    return InputText(label, str, 0, nullptr, nullptr);
  }
  bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags) {
    return InputText(label, str, flags, nullptr, nullptr);
  }
  bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags,
                 ImGuiInputTextCallback callback) {
    return InputText(label, str, flags, callback, nullptr);
  }
  virtual bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags,
                         ImGuiInputTextCallback callback, void* user_data) = 0;
  bool InputTextMultiline(const char* label, std::string* str, const ImVec2& size) {
    return InputTextMultiline(label, str, size, 0, nullptr, nullptr);
  }
  virtual bool InputTextMultiline(const char* label, std::string* str, const ImVec2& size,
                                  ImGuiInputTextFlags flags, ImGuiInputTextCallback callback,
                                  void* user_data) = 0;
  bool InputInt(const char* label, int* v) { return InputInt(label, v, 1, 100, 0); }
  bool InputInt(const char* label, int* v, int step) { return InputInt(label, v, step, 100, 0); }
  bool InputInt(const char* label, int* v, int step, int step_fast) {
    return InputInt(label, v, step, step_fast, 0);
  }
  virtual bool InputInt(const char* label, int* v, int step, int step_fast,
                        ImGuiInputTextFlags flags) = 0;
  bool InputDouble(const char* label, double* v) {
    return InputDouble(label, v, 0.0, 0.0, "%.6f", 0);
  }
  bool InputDouble(const char* label, double* v, double step) {
    return InputDouble(label, v, step, 0.0, "%.6f", 0);
  }
  bool InputDouble(const char* label, double* v, double step, double step_fast) {
    return InputDouble(label, v, step, step_fast, "%.6f", 0);
  }
  bool InputDouble(const char* label, double* v, double step, double step_fast,
                   const char* format) {
    return InputDouble(label, v, step, step_fast, format, 0);
  }
  virtual bool InputDouble(const char* label, double* v, double step, double step_fast,
                           const char* format, ImGuiInputTextFlags flags) = 0;
  bool InputFloat(const char* label, float* v) {
    return InputFloat(label, v, 0.0f, 0.0f, "%.3f", 0);
  }
  bool InputFloat(const char* label, float* v, float step) {
    return InputFloat(label, v, step, 0.0f, "%.3f", 0);
  }
  bool InputFloat(const char* label, float* v, float step, float step_fast) {
    return InputFloat(label, v, step, step_fast, "%.3f", 0);
  }
  bool InputFloat(const char* label, float* v, float step, float step_fast, const char* format) {
    return InputFloat(label, v, step, step_fast, format, 0);
  }
  virtual bool InputFloat(const char* label, float* v, float step, float step_fast,
                          const char* format, ImGuiInputTextFlags flags) = 0;

  bool Selectable(const char* label) { return Selectable(label, false, 0, ImVec2(0, 0)); }
  bool Selectable(const char* label, bool selected) {
    return Selectable(label, selected, 0, ImVec2(0, 0));
  }
  bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags) {
    return Selectable(label, selected, flags, ImVec2(0, 0));
  }
  virtual bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags,
                          const ImVec2& size) = 0;
  bool Selectable(const char* label, bool* p_selected) {
    return Selectable(label, p_selected, 0, ImVec2(0, 0));
  }
  bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags) {
    return Selectable(label, p_selected, flags, ImVec2(0, 0));
  }
  virtual bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags,
                          const ImVec2& size) = 0;

  void Image(ImTextureID user_texture_id, const ImVec2& size) {
    Image(user_texture_id, size, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1),
          ImVec4(0, 0, 0, 0));
  }
  void Image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0) {
    Image(user_texture_id, size, uv0, ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
  }
  void Image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0,
             const ImVec2& uv1) {
    Image(user_texture_id, size, uv0, uv1, ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
  }
  void Image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1,
             const ImVec4& tint_col) {
    Image(user_texture_id, size, uv0, uv1, tint_col, ImVec4(0, 0, 0, 0));
  }
  virtual void Image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0,
                     const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col) = 0;
  virtual void Dummy(const ImVec2& size) = 0;
  virtual void Spacing() = 0;

  void TableSetupColumn(const char* label) { TableSetupColumn(label, 0, 0.0f, 0); }
  void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags) {
    TableSetupColumn(label, flags, 0.0f, 0);
  }
  void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags,
                        float init_width_or_weight) {
    TableSetupColumn(label, flags, init_width_or_weight, 0);
  }
  virtual void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags,
                                float init_width_or_weight, ImGuiID user_id) = 0;
  virtual void TableHeadersRow() = 0;
  void TableNextRow() { TableNextRow(0, 0.0f); }
  void TableNextRow(ImGuiTableRowFlags row_flags) { TableNextRow(row_flags, 0.0f); }
  virtual void TableNextRow(ImGuiTableRowFlags row_flags, float min_row_height) = 0;
  virtual bool TableNextColumn() = 0;

  virtual void SetCursorPos(const ImVec2& local_pos) = 0;
  virtual void SetCursorPosX(float local_x) = 0;
  virtual void SetCursorScreenPos(const ImVec2& pos) = 0;
  virtual ImVec2 GetCursorPos() const = 0;
  virtual float GetCursorPosX() const = 0;
  virtual float GetCursorPosY() const = 0;
  virtual ImVec2 GetCursorScreenPos() const = 0;
  void SetNextWindowPos(const ImVec2& pos) { SetNextWindowPos(pos, 0, ImVec2(0, 0)); }
  void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond) {
    SetNextWindowPos(pos, cond, ImVec2(0, 0));
  }
  virtual void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond, const ImVec2& pivot) = 0;
  void SetNextWindowSize(const ImVec2& size) { SetNextWindowSize(size, 0); }
  virtual void SetNextWindowSize(const ImVec2& size, ImGuiCond cond) = 0;

  virtual void PushItemWidth(float item_width) = 0;
  virtual void PopItemWidth() = 0;
  virtual void SetNextItemWidth(float item_width) = 0;
  virtual float GetTextLineHeightWithSpacing() const = 0;
  // Height of one row occupied by a framed widget, such as a button or an
  // input field. Taller than a text row, so laying out mixed rows with the
  // text height alone under-reserves space.
  virtual float GetFrameHeightWithSpacing() const = 0;
  virtual ImVec2 GetContentRegionAvail() const = 0;
  virtual ImDrawList* GetWindowDrawList() = 0;
  virtual ImVec2 GetMousePos() const = 0;
  virtual ImGuiIO& GetIO() = 0;
  virtual ImGuiStyle& GetStyle() = 0;
  bool IsItemHovered() { return IsItemHovered(0); }
  virtual bool IsItemHovered(ImGuiHoveredFlags flags) = 0;
  // Whether any widget at all is being interacted with this frame. IsItemActive
  // only answers for the item rendered last, which is the wrong question when a
  // panel wants to know whether the user is mid-drag anywhere in it.
  virtual bool IsAnyItemActive() = 0;
  // Shows a tooltip for the item just rendered. Pair with IsItemHovered.
  virtual void SetTooltip(const char* fmt, ...) = 0;
  // Claims an input for the last item while it is hovered or active. This is
  // required for custom widgets such as Canvas to prevent handled mouse-wheel
  // input from also scrolling an ancestor window.
  virtual void SetItemKeyOwner(ImGuiKey key) = 0;
  virtual void SetItemDefaultFocus() = 0;
  virtual bool IsItemActive() = 0;
  bool IsItemClicked() { return IsItemClicked(0); }
  virtual bool IsItemClicked(ImGuiMouseButton mouse_button) = 0;
  // Whether the last click was the second of a double-click. Pair with a
  // Selectable created with ImGuiSelectableFlags_AllowDoubleClick, which is
  // what makes the widget report the second click at all.
  virtual bool IsMouseDoubleClicked(ImGuiMouseButton button) = 0;
  virtual bool IsItemDeactivatedAfterEdit() = 0;
  bool IsMouseDragging(ImGuiMouseButton button) { return IsMouseDragging(button, -1.0f); }
  virtual bool IsMouseDragging(ImGuiMouseButton button, float lock_threshold) = 0;
  virtual ImVec2 GetWindowSize() const = 0;
  bool IsWindowHovered() { return IsWindowHovered(0); }
  virtual bool IsWindowHovered(ImGuiHoveredFlags flags) = 0;
  bool IsWindowFocused() { return IsWindowFocused(0); }
  virtual bool IsWindowFocused(ImGuiFocusedFlags flags) = 0;

  // File browsing. The dialog outlives the frame that opens it, so the two
  // halves are separate calls: OpenFileDialog arms the dialog named `key`, and
  // DisplayFileDialog draws it and must be called every frame after that.
  // Keying the dialog is what stops one panel consuming another's result.
  void OpenFileDialog(const char* key, const char* title, const char* filters) {
    OpenFileDialog(key, title, filters, ".");
  }
  virtual void OpenFileDialog(const char* key, const char* title, const char* filters,
                              const char* start_path) = 0;
  // Draws the dialog armed under `key` and returns the chosen path on the one
  // frame the user confirms. Returns nullopt while the dialog is open, when it
  // is cancelled, and when no dialog is armed under that key.
  virtual std::optional<std::string> DisplayFileDialog(const char* key) = 0;

  bool CollapsingHeader(const char* label) { return CollapsingHeader(label, 0); }
  virtual bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags) = 0;
  bool TreeNodeEx(const char* label) { return TreeNodeEx(label, 0); }
  virtual bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags) = 0;

  virtual ImGuiViewport* GetMainViewport() = 0;
  virtual bool IsKeyDown(ImGuiKey key) = 0;
  bool IsKeyPressed(ImGuiKey key) { return IsKeyPressed(key, true); }
  virtual bool IsKeyPressed(ImGuiKey key, bool repeat) = 0;
  void ShowMetricsWindow() { ShowMetricsWindow(nullptr); }
  virtual void ShowMetricsWindow(bool* p_open) = 0;

  ScopedListBox CreateScopedListBox(const char* label);
  virtual ScopedListBox CreateScopedListBox(const char* label, ImVec2 size) = 0;
  ScopedChild CreateScopedChild(const char* str_id);
  ScopedChild CreateScopedChild(const char* str_id, ImVec2 size);
  ScopedChild CreateScopedChild(const char* str_id, ImVec2 size, bool border);
  virtual ScopedChild CreateScopedChild(const char* str_id, ImVec2 size, bool border,
                                        ImGuiWindowFlags flags) = 0;
  ScopedTabBar CreateScopedTabBar(const char* str_id);
  virtual ScopedTabBar CreateScopedTabBar(const char* str_id, ImGuiTabBarFlags flags) = 0;
  ScopedTabItem CreateScopedTabItem(const char* label);
  ScopedTabItem CreateScopedTabItem(const char* label, bool* p_open);
  virtual ScopedTabItem CreateScopedTabItem(const char* label, bool* p_open,
                                            ImGuiTabItemFlags flags) = 0;
  ScopedTable CreateScopedTable(const char* str_id, int column);
  ScopedTable CreateScopedTable(const char* str_id, int column, ImGuiTableFlags flags);
  ScopedTable CreateScopedTable(const char* str_id, int column, ImGuiTableFlags flags,
                                const ImVec2& outer_size);
  virtual ScopedTable CreateScopedTable(const char* str_id, int column, ImGuiTableFlags flags,
                                        const ImVec2& outer_size, float inner_width) = 0;
  ScopedDisabled CreateScopedDisabled();
  virtual ScopedDisabled CreateScopedDisabled(bool disabled) = 0;
  ScopedWindow CreateScopedWindow(const char* name);
  ScopedWindow CreateScopedWindow(const char* name, bool* p_open);
  virtual ScopedWindow CreateScopedWindow(const char* name, bool* p_open,
                                          ImGuiWindowFlags flags) = 0;
  ScopedCombo CreateScopedCombo(const char* label, const char* preview_value);
  virtual ScopedCombo CreateScopedCombo(const char* label, const char* preview_value,
                                        ImGuiComboFlags flags) = 0;
  virtual ScopedGroup CreateScopedGroup() = 0;
  virtual ScopedId CreateScopedId(const char* str_id) = 0;
  virtual ScopedId CreateScopedId(const char* str_id_begin, const char* str_id_end) = 0;
  virtual ScopedId CreateScopedId(const void* ptr_id) = 0;
  virtual ScopedId CreateScopedId(int int_id) = 0;
  virtual ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, ImU32 col) = 0;
  virtual ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, const ImVec4& col) = 0;
  virtual ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, float val) = 0;
  virtual ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, const ImVec2& val) = 0;
  ScopedPopup CreateScopedPopupContextItem();
  ScopedPopup CreateScopedPopupContextItem(const char* str_id);
  virtual ScopedPopup CreateScopedPopupContextItem(const char* str_id, ImGuiPopupFlags flags) = 0;
};

}  // namespace zebes
