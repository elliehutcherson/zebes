#include "editor/gui.h"

#include <stdarg.h>

#include "editor/imgui_scoped.h"
#include "misc/cpp/imgui_stdlib.h"

namespace zebes {

bool Gui::Begin(const char* name, bool* p_open, ImGuiWindowFlags flags) {
  return ImGui::Begin(name, p_open, flags);
}

void Gui::End() { ImGui::End(); }

bool Gui::BeginListBox(const char* label, const ImVec2& size) {
  return ImGui::BeginListBox(label, size);
}

void Gui::EndListBox() { ImGui::EndListBox(); }

bool Gui::BeginChild(const char* str_id, const ImVec2& size, bool border, ImGuiWindowFlags flags) {
  return ImGui::BeginChild(str_id, size, border, flags);
}

void Gui::EndChild() { ImGui::EndChild(); }

bool Gui::BeginTabBar(const char* str_id, ImGuiTabBarFlags flags) {
  return ImGui::BeginTabBar(str_id, flags);
}

void Gui::EndTabBar() { ImGui::EndTabBar(); }

bool Gui::BeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags) {
  return ImGui::BeginTabItem(label, p_open, flags);
}

void Gui::EndTabItem() { ImGui::EndTabItem(); }

bool Gui::BeginTable(const char* str_id, int column, ImGuiTableFlags flags,
                     const ImVec2& outer_size, float inner_width) {
  return ImGui::BeginTable(str_id, column, flags, outer_size, inner_width);
}

void Gui::EndTable() { ImGui::EndTable(); }

void Gui::BeginDisabled(bool disabled) { ImGui::BeginDisabled(disabled); }

void Gui::EndDisabled() { ImGui::EndDisabled(); }

bool Gui::BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags) {
  return ImGui::BeginCombo(label, preview_value, flags);
}

void Gui::EndCombo() { ImGui::EndCombo(); }

void Gui::BeginGroup() { ImGui::BeginGroup(); }

void Gui::EndGroup() { ImGui::EndGroup(); }

void Gui::PushID(const char* str_id) { ImGui::PushID(str_id); }

void Gui::PushID(const char* str_id_begin, const char* str_id_end) {
  ImGui::PushID(str_id_begin, str_id_end);
}

void Gui::PushID(const void* ptr_id) { ImGui::PushID(ptr_id); }

void Gui::PushID(int int_id) { ImGui::PushID(int_id); }

void Gui::PopID() { ImGui::PopID(); }

void Gui::PushStyleColor(ImGuiCol idx, ImU32 col) { ImGui::PushStyleColor(idx, col); }

void Gui::PushStyleColor(ImGuiCol idx, const ImVec4& col) { ImGui::PushStyleColor(idx, col); }

void Gui::PopStyleColor(int count) { ImGui::PopStyleColor(count); }

void Gui::PushStyleVar(ImGuiStyleVar idx, float val) { ImGui::PushStyleVar(idx, val); }

void Gui::PushStyleVar(ImGuiStyleVar idx, const ImVec2& val) { ImGui::PushStyleVar(idx, val); }

void Gui::PopStyleVar(int count) { ImGui::PopStyleVar(count); }

void Gui::Indent(float indent_w) { ImGui::Indent(indent_w); }

void Gui::Unindent(float indent_w) { ImGui::Unindent(indent_w); }

void Gui::Separator() { ImGui::Separator(); }

void Gui::SameLine(float offset_from_start_x, float spacing) {
  ImGui::SameLine(offset_from_start_x, spacing);
}

void Gui::NewFrame() { ImGui::NewFrame(); }

void Gui::AlignTextToFramePadding() { ImGui::AlignTextToFramePadding(); }

void Gui::Render() { ImGui::Render(); }

bool Gui::Button(const char* label, const ImVec2& size) { return ImGui::Button(label, size); }

bool Gui::InvisibleButton(const char* str_id, const ImVec2& size, ImGuiButtonFlags flags) {
  return ImGui::InvisibleButton(str_id, size, flags);
}

bool Gui::ArrowButton(const char* str_id, ImGuiDir dir) { return ImGui::ArrowButton(str_id, dir); }

void Gui::Text(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextV(fmt, args);
  va_end(args);
}

void Gui::TextColored(const ImVec4& col, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextColoredV(col, fmt, args);
  va_end(args);
}

void Gui::TextDisabled(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextDisabledV(fmt, args);
  va_end(args);
}

void Gui::TextWrapped(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextWrappedV(fmt, args);
  va_end(args);
}

void Gui::LabelText(const char* label, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::LabelTextV(label, fmt, args);
  va_end(args);
}

bool Gui::Checkbox(const char* label, bool* v) { return ImGui::Checkbox(label, v); }

bool Gui::SliderInt(const char* label, int* v, int v_min, int v_max, const char* format,
                    ImGuiSliderFlags flags) {
  return ImGui::SliderInt(label, v, v_min, v_max, format, flags);
}

bool Gui::InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags,
                    ImGuiInputTextCallback callback, void* user_data) {
  return ImGui::InputText(label, buf, buf_size, flags, callback, user_data);
}

bool Gui::InputText(const char* label, std::string* str, ImGuiInputTextFlags flags,
                    ImGuiInputTextCallback callback, void* user_data) {
  return ImGui::InputText(label, str, flags, callback, user_data);
}

bool Gui::InputInt(const char* label, int* v, int step, int step_fast, ImGuiInputTextFlags flags) {
  return ImGui::InputInt(label, v, step, step_fast, flags);
}

bool Gui::InputDouble(const char* label, double* v, double step, double step_fast,
                      const char* format, ImGuiInputTextFlags flags) {
  return ImGui::InputDouble(label, v, step, step_fast, format, flags);
}

bool Gui::Selectable(const char* label, bool selected, ImGuiSelectableFlags flags,
                     const ImVec2& size) {
  return ImGui::Selectable(label, selected, flags, size);
}

bool Gui::Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags,
                     const ImVec2& size) {
  return ImGui::Selectable(label, p_selected, flags, size);
}

void Gui::Image(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0,
                const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col) {
  ImGui::Image(user_texture_id, size, uv0, uv1, tint_col, border_col);
}

void Gui::Dummy(const ImVec2& size) { ImGui::Dummy(size); }

void Gui::Spacing() { ImGui::Spacing(); }

void Gui::TableSetupColumn(const char* label, ImGuiTableColumnFlags flags,
                           float init_width_or_weight, ImGuiID user_id) {
  ImGui::TableSetupColumn(label, flags, init_width_or_weight, user_id);
}

void Gui::TableHeadersRow() { ImGui::TableHeadersRow(); }

void Gui::TableNextRow(ImGuiTableRowFlags row_flags, float min_row_height) {
  ImGui::TableNextRow(row_flags, min_row_height);
}

bool Gui::TableNextColumn() { return ImGui::TableNextColumn(); }

void Gui::SetCursorPos(const ImVec2& local_pos) { ImGui::SetCursorPos(local_pos); }

void Gui::SetCursorPosX(float local_x) { ImGui::SetCursorPosX(local_x); }

void Gui::SetCursorScreenPos(const ImVec2& pos) { ImGui::SetCursorScreenPos(pos); }

ImVec2 Gui::GetCursorPos() const { return ImGui::GetCursorPos(); }

float Gui::GetCursorPosX() const { return ImGui::GetCursorPosX(); }

float Gui::GetCursorPosY() const { return ImGui::GetCursorPosY(); }

ImVec2 Gui::GetCursorScreenPos() const { return ImGui::GetCursorScreenPos(); }

void Gui::SetNextWindowPos(const ImVec2& pos, ImGuiCond cond, const ImVec2& pivot) {
  ImGui::SetNextWindowPos(pos, cond, pivot);
}

void Gui::SetNextWindowSize(const ImVec2& size, ImGuiCond cond) {
  ImGui::SetNextWindowSize(size, cond);
}

void Gui::PushItemWidth(float item_width) { ImGui::PushItemWidth(item_width); }

void Gui::PopItemWidth() { ImGui::PopItemWidth(); }

void Gui::SetNextItemWidth(float item_width) { ImGui::SetNextItemWidth(item_width); }

float Gui::GetTextLineHeightWithSpacing() const { return ImGui::GetTextLineHeightWithSpacing(); }

ImVec2 Gui::GetContentRegionAvail() const { return ImGui::GetContentRegionAvail(); }

ImDrawList* Gui::GetWindowDrawList() { return ImGui::GetWindowDrawList(); }

ImVec2 Gui::GetMousePos() const { return ImGui::GetMousePos(); }

ImGuiIO& Gui::GetIO() { return ImGui::GetIO(); }

ImGuiStyle& Gui::GetStyle() { return ImGui::GetStyle(); }

bool Gui::IsItemHovered(ImGuiHoveredFlags flags) { return ImGui::IsItemHovered(flags); }

bool Gui::IsItemActive() { return ImGui::IsItemActive(); }

void Gui::SetItemDefaultFocus() { ImGui::SetItemDefaultFocus(); }

bool Gui::CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags) {
  return ImGui::CollapsingHeader(label, flags);
}

ImGuiViewport* Gui::GetMainViewport() { return ImGui::GetMainViewport(); }

bool Gui::IsKeyPressed(ImGuiKey key, bool repeat) { return ImGui::IsKeyPressed(key, repeat); }

void Gui::ShowMetricsWindow(bool* p_open) { ImGui::ShowMetricsWindow(p_open); }

ScopedListBox Gui::CreateScopedListBox(const char* label, ImVec2 size) {
  return ScopedListBox(this, label, size);
}

ScopedChild Gui::CreateScopedChild(const char* str_id, ImVec2 size, bool border,
                                   ImGuiWindowFlags flags) {
  return ScopedChild(this, str_id, size, border, flags);
}

ScopedTabBar Gui::CreateScopedTabBar(const char* str_id, ImGuiTabBarFlags flags) {
  return ScopedTabBar(this, str_id, flags);
}

ScopedTabItem Gui::CreateScopedTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags) {
  return ScopedTabItem(this, label, p_open, flags);
}

ScopedTable Gui::CreateScopedTable(const char* str_id, int column, ImGuiTableFlags flags,
                                   const ImVec2& outer_size, float inner_width) {
  return ScopedTable(this, str_id, column, flags, outer_size, inner_width);
}

ScopedDisabled Gui::CreateScopedDisabled(bool disabled) { return ScopedDisabled(this, disabled); }

ScopedWindow Gui::CreateScopedWindow(const char* name, bool* p_open, ImGuiWindowFlags flags) {
  return ScopedWindow(this, name, p_open, flags);
}

ScopedCombo Gui::CreateScopedCombo(const char* label, const char* preview_value,
                                   ImGuiComboFlags flags) {
  return ScopedCombo(this, label, preview_value, flags);
}

ScopedGroup Gui::CreateScopedGroup() { return ScopedGroup(this); }

ScopedId Gui::CreateScopedId(const char* str_id) { return ScopedId(this, str_id); }

ScopedId Gui::CreateScopedId(const char* str_id_begin, const char* str_id_end) {
  return ScopedId(this, str_id_begin, str_id_end);
}

ScopedId Gui::CreateScopedId(const void* ptr_id) { return ScopedId(this, ptr_id); }

ScopedId Gui::CreateScopedId(int int_id) { return ScopedId(this, int_id); }

ScopedStyleColor Gui::CreateScopedStyleColor(ImGuiCol idx, ImU32 col) {
  return ScopedStyleColor(this, idx, col);
}

ScopedStyleColor Gui::CreateScopedStyleColor(ImGuiCol idx, const ImVec4& col) {
  return ScopedStyleColor(this, idx, col);
}

ScopedStyleVar Gui::CreateScopedStyleVar(ImGuiStyleVar idx, float val) {
  return ScopedStyleVar(this, idx, val);
}

ScopedStyleVar Gui::CreateScopedStyleVar(ImGuiStyleVar idx, const ImVec2& val) {
  return ScopedStyleVar(this, idx, val);
}

}  // namespace zebes
