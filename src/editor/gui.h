#pragma once

#include "editor/gui_interface.h"

namespace zebes {

class Gui : public GuiInterface {
 public:
  Gui() = default;
  ~Gui() override = default;

  bool Begin(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0) override;
  void End() override;
  bool BeginListBox(const char* label, const ImVec2& size = ImVec2(0, 0)) override;
  void EndListBox() override;
  bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0, 0), bool border = false,
                  ImGuiWindowFlags flags = 0) override;
  void EndChild() override;
  bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags = 0) override;
  void EndTabBar() override;
  bool BeginTabItem(const char* label, bool* p_open = nullptr,
                    ImGuiTabItemFlags flags = 0) override;
  void EndTabItem() override;
  bool BeginTable(const char* str_id, int column, ImGuiTableFlags flags = 0,
                  const ImVec2& outer_size = ImVec2(0.0f, 0.0f), float inner_width = 0.0f) override;
  void EndTable() override;
  void BeginDisabled(bool disabled = true) override;
  void EndDisabled() override;
  bool BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags = 0) override;
  void EndCombo() override;
  void BeginGroup() override;
  void EndGroup() override;

  void PushID(const char* str_id) override;
  void PushID(const char* str_id_begin, const char* str_id_end) override;
  void PushID(const void* ptr_id) override;
  void PushID(int int_id) override;
  void PopID() override;

  void PushStyleColor(ImGuiCol idx, ImU32 col) override;
  void PushStyleColor(ImGuiCol idx, const ImVec4& col) override;
  void PopStyleColor(int count = 1) override;

  void PushStyleVar(ImGuiStyleVar idx, float val) override;
  void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val) override;
  void PopStyleVar(int count = 1) override;

  void Indent(float indent_w = 0.0f) override;
  void Unindent(float indent_w = 0.0f) override;
  void Separator() override;
  void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f) override;
  void NewFrame() override;
  void AlignTextToFramePadding() override;
  void Render() override;

  bool Button(const char* label, const ImVec2& size = ImVec2(0, 0)) override;
  bool InvisibleButton(const char* str_id, const ImVec2& size, ImGuiButtonFlags flags = 0) override;
  bool ArrowButton(const char* str_id, ImGuiDir dir) override;
  void Text(const char* fmt, ...) override;
  void TextColored(const ImVec4& col, const char* fmt, ...) override;
  void TextDisabled(const char* fmt, ...) override;
  void TextWrapped(const char* fmt, ...) override;
  void LabelText(const char* label, const char* fmt, ...) override;

  bool Checkbox(const char* label, bool* v) override;
  bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d",
                 ImGuiSliderFlags flags = 0) override;
  bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0,
                 ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr) override;
  bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags = 0,
                 ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr) override;
  bool InputInt(const char* label, int* v, int step = 1, int step_fast = 100,
                ImGuiInputTextFlags flags = 0) override;
  bool InputDouble(const char* label, double* v, double step = 0.0, double step_fast = 0.0,
                   const char* format = "%.6f", ImGuiInputTextFlags flags = 0) override;

  bool Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0,
                  const ImVec2& size = ImVec2(0, 0)) override;
  bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags = 0,
                  const ImVec2& size = ImVec2(0, 0)) override;

  void Image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0),
             const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1),
             const ImVec4& border_col = ImVec4(0, 0, 0, 0)) override;
  void Dummy(const ImVec2& size) override;
  void Spacing() override;

  void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags = 0,
                        float init_width_or_weight = 0.0f, ImGuiID user_id = 0) override;
  void TableHeadersRow() override;
  void TableNextRow(ImGuiTableRowFlags row_flags = 0, float min_row_height = 0.0f) override;
  bool TableNextColumn() override;

  void SetCursorPos(const ImVec2& local_pos) override;
  void SetCursorPosX(float local_x) override;
  void SetCursorScreenPos(const ImVec2& pos) override;
  ImVec2 GetCursorPos() const override;
  float GetCursorPosX() const override;
  float GetCursorPosY() const override;
  ImVec2 GetCursorScreenPos() const override;
  void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond = 0,
                        const ImVec2& pivot = ImVec2(0, 0)) override;
  void SetNextWindowSize(const ImVec2& size, ImGuiCond cond = 0) override;

  void PushItemWidth(float item_width) override;
  void PopItemWidth() override;
  void SetNextItemWidth(float item_width) override;
  float GetTextLineHeightWithSpacing() const override;
  ImVec2 GetContentRegionAvail() const override;
  ImDrawList* GetWindowDrawList() override;
  ImVec2 GetMousePos() const override;
  ImGuiIO& GetIO() override;
  ImGuiStyle& GetStyle() override;
  bool IsItemHovered(ImGuiHoveredFlags flags = 0) override;
  void SetItemDefaultFocus() override;
  bool IsItemActive() override;

  bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) override;

  ImGuiViewport* GetMainViewport() override;
  bool IsKeyPressed(ImGuiKey key, bool repeat = true) override;
  void ShowMetricsWindow(bool* p_open = nullptr) override;

  ScopedListBox CreateScopedListBox(const char* label, ImVec2 size = ImVec2(0, 0)) override;
  ScopedChild CreateScopedChild(const char* str_id, ImVec2 size = ImVec2(0, 0), bool border = false,
                                ImGuiWindowFlags flags = 0) override;
  ScopedTabBar CreateScopedTabBar(const char* str_id, ImGuiTabBarFlags flags = 0) override;
  ScopedTabItem CreateScopedTabItem(const char* label, bool* p_open = nullptr,
                                    ImGuiTabItemFlags flags = 0) override;
  ScopedTable CreateScopedTable(const char* str_id, int column, ImGuiTableFlags flags = 0,
                                const ImVec2& outer_size = ImVec2(0.0f, 0.0f),
                                float inner_width = 0.0f) override;
  ScopedDisabled CreateScopedDisabled(bool disabled = true) override;
  ScopedWindow CreateScopedWindow(const char* name, bool* p_open = nullptr,
                                  ImGuiWindowFlags flags = 0) override;
  ScopedCombo CreateScopedCombo(const char* label, const char* preview_value,
                                ImGuiComboFlags flags = 0) override;
  ScopedGroup CreateScopedGroup() override;
  ScopedId CreateScopedId(const char* str_id) override;
  ScopedId CreateScopedId(const char* str_id_begin, const char* str_id_end) override;
  ScopedId CreateScopedId(const void* ptr_id) override;
  ScopedId CreateScopedId(int int_id) override;
  ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, ImU32 col) override;
  ScopedStyleColor CreateScopedStyleColor(ImGuiCol idx, const ImVec4& col) override;
  ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, float val) override;
  ScopedStyleVar CreateScopedStyleVar(ImGuiStyleVar idx, const ImVec2& val) override;
};

}  // namespace zebes
