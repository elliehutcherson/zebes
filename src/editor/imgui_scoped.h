#pragma once

#include "imgui.h"

namespace zebes {

// RAII wrapper for ImGui::BeginListBox/EndListBox
class ScopedListBox {
 public:
  ScopedListBox(const char* label, ImVec2 size = ImVec2(0, 0))
      : active_(ImGui::BeginListBox(label, size)) {}

  ~ScopedListBox() {
    if (active_) {
      ImGui::EndListBox();
    }
  }

  // Allow checking if the list box is active
  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  // Prevent copying
  ScopedListBox(const ScopedListBox&) = delete;
  ScopedListBox& operator=(const ScopedListBox&) = delete;

 private:
  bool active_;
};

// RAII wrapper for ImGui::BeginChild/EndChild
class ScopedChild {
 public:
  ScopedChild(const char* str_id, ImVec2 size = ImVec2(0, 0), bool border = false,
              ImGuiWindowFlags flags = 0)
      : active_(ImGui::BeginChild(str_id, size, border, flags)) {}

  ~ScopedChild() {
    if (active_) {
      ImGui::EndChild();
    }
  }

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedChild(const ScopedChild&) = delete;
  ScopedChild& operator=(const ScopedChild&) = delete;

 private:
  bool active_;
};

// RAII wrapper for ImGui::BeginTabBar/EndTabBar
class ScopedTabBar {
 public:
  ScopedTabBar(const char* str_id, ImGuiTabBarFlags flags = 0)
      : active_(ImGui::BeginTabBar(str_id, flags)) {}

  ~ScopedTabBar() {
    if (active_) {
      ImGui::EndTabBar();
    }
  }

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedTabBar(const ScopedTabBar&) = delete;
  ScopedTabBar& operator=(const ScopedTabBar&) = delete;

 private:
  bool active_;
};

// RAII wrapper for ImGui::BeginTabItem/EndTabItem
class ScopedTabItem {
 public:
  ScopedTabItem(const char* label, bool* p_open = nullptr, ImGuiTabItemFlags flags = 0)
      : active_(ImGui::BeginTabItem(label, p_open, flags)) {}

  ~ScopedTabItem() {
    if (active_) {
      ImGui::EndTabItem();
    }
  }

  operator bool() const { return active_; }
  bool IsActive() const { return active_; }

  ScopedTabItem(const ScopedTabItem&) = delete;
  ScopedTabItem& operator=(const ScopedTabItem&) = delete;

 private:
  bool active_;
};

}  // namespace zebes