#pragma once

#include "editor/gui_interface.h"
#include "imgui.h"

namespace zebes {

// RAII wrapper for GuiInterface::BeginListBox/EndListBox
class ScopedListBox {
 public:
  ScopedListBox(GuiInterface* gui, const char* label, ImVec2 size = ImVec2(0, 0));
  ~ScopedListBox();

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedListBox(const ScopedListBox&) = delete;
  ScopedListBox& operator=(const ScopedListBox&) = delete;

  // Move constructor/assignment for returning from function
  ScopedListBox(ScopedListBox&& other) noexcept;
  ScopedListBox& operator=(ScopedListBox&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool active_;
};

// RAII wrapper for GuiInterface::BeginChild/EndChild
class ScopedChild {
 public:
  ScopedChild(GuiInterface* gui, const char* str_id, ImVec2 size = ImVec2(0, 0),
              bool border = false, ImGuiWindowFlags flags = 0);
  ~ScopedChild();

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedChild(const ScopedChild&) = delete;
  ScopedChild& operator=(const ScopedChild&) = delete;

  ScopedChild(ScopedChild&& other) noexcept;
  ScopedChild& operator=(ScopedChild&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool active_;
};

// RAII wrapper for GuiInterface::BeginTabBar/EndTabBar
class ScopedTabBar {
 public:
  ScopedTabBar(GuiInterface* gui, const char* str_id, ImGuiTabBarFlags flags = 0);
  ~ScopedTabBar();

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedTabBar(const ScopedTabBar&) = delete;
  ScopedTabBar& operator=(const ScopedTabBar&) = delete;

  ScopedTabBar(ScopedTabBar&& other) noexcept;
  ScopedTabBar& operator=(ScopedTabBar&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool active_;
};

// RAII wrapper for GuiInterface::BeginTabItem/EndTabItem
class ScopedTabItem {
 public:
  ScopedTabItem(GuiInterface* gui, const char* label, bool* p_open = nullptr,
                ImGuiTabItemFlags flags = 0);
  ~ScopedTabItem();

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedTabItem(const ScopedTabItem&) = delete;
  ScopedTabItem& operator=(const ScopedTabItem&) = delete;

  ScopedTabItem(ScopedTabItem&& other) noexcept;
  ScopedTabItem& operator=(ScopedTabItem&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool active_;
};

// RAII wrapper for GuiInterface::BeginTable/EndTable
class ScopedTable {
 public:
  ScopedTable(GuiInterface* gui, const char* str_id, int column, ImGuiTableFlags flags = 0,
              const ImVec2& outer_size = ImVec2(0.0f, 0.0f), float inner_width = 0.0f);
  ~ScopedTable();

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedTable(const ScopedTable&) = delete;
  ScopedTable& operator=(const ScopedTable&) = delete;

  ScopedTable(ScopedTable&& other) noexcept;
  ScopedTable& operator=(ScopedTable&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool active_;
};

// RAII wrapper for GuiInterface::BeginDisabled/EndDisabled
class ScopedDisabled {
 public:
  ScopedDisabled(GuiInterface* gui, bool disabled = true);
  ~ScopedDisabled();

  ScopedDisabled(const ScopedDisabled&) = delete;
  ScopedDisabled& operator=(const ScopedDisabled&) = delete;

  ScopedDisabled(ScopedDisabled&& other) noexcept;
  ScopedDisabled& operator=(ScopedDisabled&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool moved_from_ = false;
};

// RAII wrapper for GuiInterface::Begin/End
class ScopedWindow {
 public:
  ScopedWindow(GuiInterface* gui, const char* name, bool* p_open = nullptr,
               ImGuiWindowFlags flags = 0);
  ~ScopedWindow();

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedWindow(const ScopedWindow&) = delete;
  ScopedWindow& operator=(const ScopedWindow&) = delete;

  ScopedWindow(ScopedWindow&& other) noexcept;
  ScopedWindow& operator=(ScopedWindow&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool active_;
};

// RAII wrapper for GuiInterface::BeginCombo/EndCombo
class ScopedCombo {
 public:
  ScopedCombo(GuiInterface* gui, const char* label, const char* preview_value,
              ImGuiComboFlags flags = 0);
  ~ScopedCombo();

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedCombo(const ScopedCombo&) = delete;
  ScopedCombo& operator=(const ScopedCombo&) = delete;

  ScopedCombo(ScopedCombo&& other) noexcept;
  ScopedCombo& operator=(ScopedCombo&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool active_;
};

// RAII wrapper for GuiInterface::BeginGroup/EndGroup
class ScopedGroup {
 public:
  ScopedGroup(GuiInterface* gui);
  ~ScopedGroup();

  ScopedGroup(const ScopedGroup&) = delete;
  ScopedGroup& operator=(const ScopedGroup&) = delete;

  ScopedGroup(ScopedGroup&& other) noexcept;
  ScopedGroup& operator=(ScopedGroup&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool moved_from_ = false;
};

// RAII wrapper for GuiInterface::PushID/PopID
class ScopedId {
 public:
  explicit ScopedId(GuiInterface* gui, const char* str_id);
  explicit ScopedId(GuiInterface* gui, const char* str_id_begin, const char* str_id_end);
  explicit ScopedId(GuiInterface* gui, const void* ptr_id);
  explicit ScopedId(GuiInterface* gui, int int_id);
  ~ScopedId();

  ScopedId(const ScopedId&) = delete;
  ScopedId& operator=(const ScopedId&) = delete;

  ScopedId(ScopedId&& other) noexcept;
  ScopedId& operator=(ScopedId&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool moved_from_ = false;
};

// RAII wrapper for GuiInterface::PushStyleColor/PopStyleColor
class ScopedStyleColor {
 public:
  ScopedStyleColor(GuiInterface* gui, ImGuiCol idx, ImU32 col);
  ScopedStyleColor(GuiInterface* gui, ImGuiCol idx, const ImVec4& col);
  ~ScopedStyleColor();

  ScopedStyleColor(const ScopedStyleColor&) = delete;
  ScopedStyleColor& operator=(const ScopedStyleColor&) = delete;

  ScopedStyleColor(ScopedStyleColor&& other) noexcept;
  ScopedStyleColor& operator=(ScopedStyleColor&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool moved_from_ = false;
};

// RAII wrapper for GuiInterface::PushStyleVar/PopStyleVar
class ScopedStyleVar {
 public:
  ScopedStyleVar(GuiInterface* gui, ImGuiStyleVar idx, float val);
  ScopedStyleVar(GuiInterface* gui, ImGuiStyleVar idx, const ImVec2& val);
  ~ScopedStyleVar();

  ScopedStyleVar(const ScopedStyleVar&) = delete;
  ScopedStyleVar& operator=(const ScopedStyleVar&) = delete;

  ScopedStyleVar(ScopedStyleVar&& other) noexcept;
  ScopedStyleVar& operator=(ScopedStyleVar&& other) noexcept;

 private:
  GuiInterface* gui_;
  bool moved_from_ = false;
};

}  // namespace zebes