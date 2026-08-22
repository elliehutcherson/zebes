#pragma once

#include "editor/gui_interface.h"

namespace zebes {

class Gui : public GuiInterface {
 public:
  Gui() = default;
  ~Gui() override = default;

  using GuiInterface::Begin;
  using GuiInterface::BeginChild;
  using GuiInterface::BeginCombo;
  using GuiInterface::BeginDisabled;
  using GuiInterface::BeginListBox;
  using GuiInterface::BeginPopupContextItem;
  using GuiInterface::BeginTabBar;
  using GuiInterface::BeginTabItem;
  using GuiInterface::BeginTable;
  using GuiInterface::Button;
  using GuiInterface::CollapsingHeader;
  using GuiInterface::ColorEdit3;
  using GuiInterface::CreateScopedChild;
  using GuiInterface::CreateScopedCombo;
  using GuiInterface::CreateScopedDisabled;
  using GuiInterface::CreateScopedListBox;
  using GuiInterface::CreateScopedPopupContextItem;
  using GuiInterface::CreateScopedTabBar;
  using GuiInterface::CreateScopedTabItem;
  using GuiInterface::CreateScopedTable;
  using GuiInterface::CreateScopedWindow;
  using GuiInterface::Image;
  using GuiInterface::Indent;
  using GuiInterface::InputDouble;
  using GuiInterface::InputFloat;
  using GuiInterface::InputInt;
  using GuiInterface::InputText;
  using GuiInterface::InputTextMultiline;
  using GuiInterface::InvisibleButton;
  using GuiInterface::IsItemClicked;
  using GuiInterface::IsItemHovered;
  using GuiInterface::IsKeyPressed;
  using GuiInterface::IsMouseDragging;
  using GuiInterface::IsWindowFocused;
  using GuiInterface::IsWindowHovered;
  using GuiInterface::MenuItem;
  using GuiInterface::OpenFileDialog;
  using GuiInterface::PopStyleColor;
  using GuiInterface::PopStyleVar;
  using GuiInterface::SameLine;
  using GuiInterface::Selectable;
  using GuiInterface::SetNextWindowPos;
  using GuiInterface::SetNextWindowSize;
  using GuiInterface::ShowMetricsWindow;
  using GuiInterface::SliderFloat;
  using GuiInterface::SliderInt;
  using GuiInterface::TableNextRow;
  using GuiInterface::TableSetupColumn;
  using GuiInterface::TreeNodeEx;
  using GuiInterface::Unindent;

  bool Begin(const char* name, bool* p_open, ImGuiWindowFlags flags) override;
  void End() override;
  bool BeginListBox(const char* label, const ImVec2& size) override;
  void EndListBox() override;
  bool BeginChild(const char* str_id, const ImVec2& size, bool border,
                  ImGuiWindowFlags flags) override;
  void EndChild() override;
  bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags) override;
  void EndTabBar() override;
  bool BeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags) override;
  void EndTabItem() override;
  bool BeginTable(const char* str_id, int column, ImGuiTableFlags flags, const ImVec2& outer_size,
                  float inner_width) override;
  void EndTable() override;
  void BeginDisabled(bool disabled) override;
  void EndDisabled() override;
  bool BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags) override;
  void EndCombo() override;
  void BeginGroup() override;
  void EndGroup() override;
  bool BeginPopupContextItem(const char* str_id, ImGuiPopupFlags flags) override;
  void EndPopup() override;
  bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled) override;

  void PushID(const char* str_id) override;
  void PushID(const char* str_id_begin, const char* str_id_end) override;
  void PushID(const void* ptr_id) override;
  void PushID(int int_id) override;
  void PopID() override;

  void PushStyleColor(ImGuiCol idx, ImU32 col) override;
  void PushStyleColor(ImGuiCol idx, const ImVec4& col) override;
  void PopStyleColor(int count) override;

  void PushStyleVar(ImGuiStyleVar idx, float val) override;
  void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val) override;
  void PopStyleVar(int count) override;

  void Indent(float indent_w) override;
  void Unindent(float indent_w) override;
  void Separator() override;
  void SameLine(float offset_from_start_x, float spacing) override;
  void NewFrame() override;
  void AlignTextToFramePadding() override;
  void Render() override;

  bool Button(const char* label, const ImVec2& size) override;
  bool InvisibleButton(const char* str_id, const ImVec2& size, ImGuiButtonFlags flags) override;
  bool ArrowButton(const char* str_id, ImGuiDir dir) override;
  void Text(const char* fmt, ...) override;
  void TextColored(const ImVec4& col, const char* fmt, ...) override;
  void TextDisabled(const char* fmt, ...) override;
  void TextWrapped(const char* fmt, ...) override;
  void LabelText(const char* label, const char* fmt, ...) override;

  bool Checkbox(const char* label, bool* v) override;
  bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format,
                   ImGuiSliderFlags flags) override;
  bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format,
                 ImGuiSliderFlags flags) override;
  bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags) override;
  bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags,
                 ImGuiInputTextCallback callback, void* user_data) override;
  bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags,
                 ImGuiInputTextCallback callback, void* user_data) override;
  bool InputTextMultiline(const char* label, std::string* str, const ImVec2& size,
                          ImGuiInputTextFlags flags, ImGuiInputTextCallback callback,
                          void* user_data) override;
  bool InputInt(const char* label, int* v, int step, int step_fast,
                ImGuiInputTextFlags flags) override;
  bool InputDouble(const char* label, double* v, double step, double step_fast, const char* format,
                   ImGuiInputTextFlags flags) override;
  bool InputFloat(const char* label, float* v, float step, float step_fast, const char* format,
                  ImGuiInputTextFlags flags) override;

  bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags,
                  const ImVec2& size) override;
  bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags,
                  const ImVec2& size) override;

  void Image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1,
             const ImVec4& tint_col, const ImVec4& border_col) override;
  void Dummy(const ImVec2& size) override;
  void Spacing() override;

  void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags, float init_width_or_weight,
                        ImGuiID user_id) override;
  void TableHeadersRow() override;
  void TableNextRow(ImGuiTableRowFlags row_flags, float min_row_height) override;
  bool TableNextColumn() override;

  void SetCursorPos(const ImVec2& local_pos) override;
  void SetCursorPosX(float local_x) override;
  void SetCursorScreenPos(const ImVec2& pos) override;
  ImVec2 GetCursorPos() const override;
  float GetCursorPosX() const override;
  float GetCursorPosY() const override;
  ImVec2 GetCursorScreenPos() const override;
  void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond, const ImVec2& pivot) override;
  void SetNextWindowSize(const ImVec2& size, ImGuiCond cond) override;

  void PushItemWidth(float item_width) override;
  void PopItemWidth() override;
  void SetNextItemWidth(float item_width) override;
  float GetTextLineHeightWithSpacing() const override;
  float GetFrameHeightWithSpacing() const override;
  ImVec2 GetContentRegionAvail() const override;
  ImDrawList* GetWindowDrawList() override;
  ImVec2 GetMousePos() const override;
  ImGuiIO& GetIO() override;
  ImGuiStyle& GetStyle() override;
  bool IsItemHovered(ImGuiHoveredFlags flags) override;
  bool IsAnyItemActive() override;
  void SetTooltip(const char* fmt, ...) override;
  void SetItemKeyOwner(ImGuiKey key) override;
  void SetItemDefaultFocus() override;
  bool IsItemActive() override;
  bool IsItemClicked(ImGuiMouseButton mouse_button) override;
  bool IsMouseDoubleClicked(ImGuiMouseButton button) override;
  bool IsItemDeactivatedAfterEdit() override;
  bool IsMouseDragging(ImGuiMouseButton button, float lock_threshold) override;
  ImVec2 GetWindowSize() const override;
  bool IsWindowHovered(ImGuiHoveredFlags flags) override;
  bool IsWindowFocused(ImGuiFocusedFlags flags) override;

  void OpenFileDialog(const char* key, const char* title, const char* filters,
                      const char* start_path) override;
  std::optional<std::string> DisplayFileDialog(const char* key) override;

  bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags) override;
  bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags) override;

  ImGuiViewport* GetMainViewport() override;
  bool IsKeyDown(ImGuiKey key) override;
  bool IsKeyPressed(ImGuiKey key, bool repeat) override;
  void ShowMetricsWindow(bool* p_open) override;

  ScopedListBox CreateScopedListBox(const char* label, ImVec2 size) override;
  ScopedChild CreateScopedChild(const char* str_id, ImVec2 size, bool border,
                                ImGuiWindowFlags flags) override;
  ScopedTabBar CreateScopedTabBar(const char* str_id, ImGuiTabBarFlags flags) override;
  ScopedTabItem CreateScopedTabItem(const char* label, bool* p_open,
                                    ImGuiTabItemFlags flags) override;
  ScopedTable CreateScopedTable(const char* str_id, int column, ImGuiTableFlags flags,
                                const ImVec2& outer_size, float inner_width) override;
  ScopedDisabled CreateScopedDisabled(bool disabled) override;
  ScopedWindow CreateScopedWindow(const char* name, bool* p_open, ImGuiWindowFlags flags) override;
  ScopedCombo CreateScopedCombo(const char* label, const char* preview_value,
                                ImGuiComboFlags flags) override;
  ScopedGroup CreateScopedGroup() override;
  ScopedId CreateScopedId(const char* str_id) override;
  ScopedId CreateScopedId(const char* str_id_begin, const char* str_id_end) override;
  ScopedId CreateScopedId(const void* ptr_id) override;
  ScopedId CreateScopedId(int int_id) override;
  ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, ImU32 col) override;
  ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, const ImVec4& col) override;
  ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, float val) override;
  ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, const ImVec2& val) override;
  ScopedPopup CreateScopedPopupContextItem(const char* str_id, ImGuiPopupFlags flags) override;
};

}  // namespace zebes
