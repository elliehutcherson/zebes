#include "editor/imgui_scoped.h"

#include "editor/gui_interface.h"

namespace zebes {

// ScopedListBox
ScopedListBox::ScopedListBox(GuiInterface* gui, const char* label, ImVec2 size)
    : gui_(gui), active_(gui->BeginListBox(label, size)) {}

ScopedListBox::~ScopedListBox() {
  if (active_ && gui_) {
    gui_->EndListBox();
  }
}

ScopedListBox::ScopedListBox(ScopedListBox&& other) noexcept
    : gui_(other.gui_), active_(other.active_) {
  other.active_ = false;
  other.gui_ = nullptr;
}

ScopedListBox& ScopedListBox::operator=(ScopedListBox&& other) noexcept {
  if (this != &other) {
    if (active_ && gui_) gui_->EndListBox();
    gui_ = other.gui_;
    active_ = other.active_;
    other.active_ = false;
    other.gui_ = nullptr;
  }
  return *this;
}

// ScopedChild
ScopedChild::ScopedChild(GuiInterface* gui, const char* str_id, ImVec2 size, bool border,
                         ImGuiWindowFlags flags)
    : gui_(gui), active_(gui->BeginChild(str_id, size, border, flags)) {}

ScopedChild::~ScopedChild() {
  if (gui_) {
    gui_->EndChild();
  }
}

ScopedChild::ScopedChild(ScopedChild&& other) noexcept : gui_(other.gui_), active_(other.active_) {
  other.active_ = false;
  other.gui_ = nullptr;
}

ScopedChild& ScopedChild::operator=(ScopedChild&& other) noexcept {
  if (this != &other) {
    if (gui_) gui_->EndChild();
    gui_ = other.gui_;
    active_ = other.active_;
    other.active_ = false;
    other.gui_ = nullptr;
  }
  return *this;
}

// ScopedTabBar
ScopedTabBar::ScopedTabBar(GuiInterface* gui, const char* str_id, ImGuiTabBarFlags flags)
    : gui_(gui), active_(gui->BeginTabBar(str_id, flags)) {}

ScopedTabBar::~ScopedTabBar() {
  if (active_ && gui_) {
    gui_->EndTabBar();
  }
}

ScopedTabBar::ScopedTabBar(ScopedTabBar&& other) noexcept
    : gui_(other.gui_), active_(other.active_) {
  other.active_ = false;
  other.gui_ = nullptr;
}

ScopedTabBar& ScopedTabBar::operator=(ScopedTabBar&& other) noexcept {
  if (this != &other) {
    if (active_ && gui_) gui_->EndTabBar();
    gui_ = other.gui_;
    active_ = other.active_;
    other.active_ = false;
    other.gui_ = nullptr;
  }
  return *this;
}

// ScopedTabItem
ScopedTabItem::ScopedTabItem(GuiInterface* gui, const char* label, bool* p_open,
                             ImGuiTabItemFlags flags)
    : gui_(gui), active_(gui->BeginTabItem(label, p_open, flags)) {}

ScopedTabItem::~ScopedTabItem() {
  if (active_ && gui_) {
    gui_->EndTabItem();
  }
}

ScopedTabItem::ScopedTabItem(ScopedTabItem&& other) noexcept
    : gui_(other.gui_), active_(other.active_) {
  other.active_ = false;
  other.gui_ = nullptr;
}

ScopedTabItem& ScopedTabItem::operator=(ScopedTabItem&& other) noexcept {
  if (this != &other) {
    if (active_ && gui_) gui_->EndTabItem();
    gui_ = other.gui_;
    active_ = other.active_;
    other.active_ = false;
    other.gui_ = nullptr;
  }
  return *this;
}

// ScopedTable
ScopedTable::ScopedTable(GuiInterface* gui, const char* str_id, int column, ImGuiTableFlags flags,
                         const ImVec2& outer_size, float inner_width)
    : gui_(gui), active_(gui->BeginTable(str_id, column, flags, outer_size, inner_width)) {}

ScopedTable::~ScopedTable() {
  if (active_ && gui_) {
    gui_->EndTable();
  }
}

ScopedTable::ScopedTable(ScopedTable&& other) noexcept : gui_(other.gui_), active_(other.active_) {
  other.active_ = false;
  other.gui_ = nullptr;
}

ScopedTable& ScopedTable::operator=(ScopedTable&& other) noexcept {
  if (this != &other) {
    if (active_ && gui_) gui_->EndTable();
    gui_ = other.gui_;
    active_ = other.active_;
    other.active_ = false;
    other.gui_ = nullptr;
  }
  return *this;
}

// ScopedDisabled
ScopedDisabled::ScopedDisabled(GuiInterface* gui, bool disabled) : gui_(gui), moved_from_(false) {
  gui_->BeginDisabled(disabled);
}

ScopedDisabled::~ScopedDisabled() {
  if (!moved_from_ && gui_) {
    gui_->EndDisabled();
  }
}

ScopedDisabled::ScopedDisabled(ScopedDisabled&& other) noexcept
    : gui_(other.gui_), moved_from_(false) {
  other.moved_from_ = true;
}

ScopedDisabled& ScopedDisabled::operator=(ScopedDisabled&& other) noexcept {
  if (this != &other) {
    if (!moved_from_ && gui_) gui_->EndDisabled();
    gui_ = other.gui_;
    moved_from_ = false;
    other.moved_from_ = true;
  }
  return *this;
}

// ScopedWindow
ScopedWindow::ScopedWindow(GuiInterface* gui, const char* name, bool* p_open,
                           ImGuiWindowFlags flags)
    : gui_(gui), active_(gui->Begin(name, p_open, flags)) {}

ScopedWindow::~ScopedWindow() {
  if (gui_) {
    gui_->End();
  }
}

ScopedWindow::ScopedWindow(ScopedWindow&& other) noexcept
    : gui_(other.gui_), active_(other.active_) {
  other.active_ = false;
  other.gui_ = nullptr;
}

ScopedWindow& ScopedWindow::operator=(ScopedWindow&& other) noexcept {
  if (this != &other) {
    if (gui_) gui_->End();
    gui_ = other.gui_;
    active_ = other.active_;
    other.active_ = false;
    other.gui_ = nullptr;
  }
  return *this;
}

// ScopedCombo
ScopedCombo::ScopedCombo(GuiInterface* gui, const char* label, const char* preview_value,
                         ImGuiComboFlags flags)
    : gui_(gui), active_(gui->BeginCombo(label, preview_value, flags)) {}

ScopedCombo::~ScopedCombo() {
  if (active_ && gui_) {
    gui_->EndCombo();
  }
}

ScopedCombo::ScopedCombo(ScopedCombo&& other) noexcept : gui_(other.gui_), active_(other.active_) {
  other.active_ = false;
  other.gui_ = nullptr;
}

ScopedCombo& ScopedCombo::operator=(ScopedCombo&& other) noexcept {
  if (this != &other) {
    if (active_ && gui_) gui_->EndCombo();
    gui_ = other.gui_;
    active_ = other.active_;
    other.active_ = false;
    other.gui_ = nullptr;
  }
  return *this;
}

// ScopedGroup
ScopedGroup::ScopedGroup(GuiInterface* gui) : gui_(gui), moved_from_(false) { gui_->BeginGroup(); }

ScopedGroup::~ScopedGroup() {
  if (!moved_from_ && gui_) {
    gui_->EndGroup();
  }
}

ScopedGroup::ScopedGroup(ScopedGroup&& other) noexcept : gui_(other.gui_), moved_from_(false) {
  other.moved_from_ = true;
}

ScopedGroup& ScopedGroup::operator=(ScopedGroup&& other) noexcept {
  if (this != &other) {
    if (!moved_from_ && gui_) gui_->EndGroup();
    gui_ = other.gui_;
    moved_from_ = false;
    other.moved_from_ = true;
  }
  return *this;
}

// ScopedId
ScopedId::ScopedId(GuiInterface* gui, const char* str_id) : gui_(gui), moved_from_(false) {
  gui_->PushID(str_id);
}
ScopedId::ScopedId(GuiInterface* gui, const char* str_id_begin, const char* str_id_end)
    : gui_(gui), moved_from_(false) {
  gui_->PushID(str_id_begin, str_id_end);
}
ScopedId::ScopedId(GuiInterface* gui, const void* ptr_id) : gui_(gui), moved_from_(false) {
  gui_->PushID(ptr_id);
}
ScopedId::ScopedId(GuiInterface* gui, int int_id) : gui_(gui), moved_from_(false) {
  gui_->PushID(int_id);
}

ScopedId::~ScopedId() {
  if (!moved_from_ && gui_) {
    gui_->PopID();
  }
}

ScopedId::ScopedId(ScopedId&& other) noexcept : gui_(other.gui_), moved_from_(false) {
  other.moved_from_ = true;
}

ScopedId& ScopedId::operator=(ScopedId&& other) noexcept {
  if (this != &other) {
    if (!moved_from_ && gui_) gui_->PopID();
    gui_ = other.gui_;
    moved_from_ = false;
    other.moved_from_ = true;
  }
  return *this;
}

// ScopedStyleColor
ScopedStyleColor::ScopedStyleColor(GuiInterface* gui, ImGuiCol idx, ImU32 col)
    : gui_(gui), moved_from_(false) {
  gui_->PushStyleColor(idx, col);
}
ScopedStyleColor::ScopedStyleColor(GuiInterface* gui, ImGuiCol idx, const ImVec4& col)
    : gui_(gui), moved_from_(false) {
  gui_->PushStyleColor(idx, col);
}

ScopedStyleColor::~ScopedStyleColor() {
  if (!moved_from_ && gui_) {
    gui_->PopStyleColor();
  }
}

ScopedStyleColor::ScopedStyleColor(ScopedStyleColor&& other) noexcept
    : gui_(other.gui_), moved_from_(false) {
  other.moved_from_ = true;
}

ScopedStyleColor& ScopedStyleColor::operator=(ScopedStyleColor&& other) noexcept {
  if (this != &other) {
    if (!moved_from_ && gui_) gui_->PopStyleColor();
    gui_ = other.gui_;
    moved_from_ = false;
    other.moved_from_ = true;
  }
  return *this;
}

// ScopedStyleVar
ScopedStyleVar::ScopedStyleVar(GuiInterface* gui, ImGuiStyleVar idx, float val)
    : gui_(gui), moved_from_(false) {
  gui_->PushStyleVar(idx, val);
}
ScopedStyleVar::ScopedStyleVar(GuiInterface* gui, ImGuiStyleVar idx, const ImVec2& val)
    : gui_(gui), moved_from_(false) {
  gui_->PushStyleVar(idx, val);
}

ScopedStyleVar::~ScopedStyleVar() {
  if (!moved_from_ && gui_) {
    gui_->PopStyleVar();
  }
}

ScopedStyleVar::ScopedStyleVar(ScopedStyleVar&& other) noexcept
    : gui_(other.gui_), moved_from_(false) {
  other.moved_from_ = true;
}

ScopedStyleVar& ScopedStyleVar::operator=(ScopedStyleVar&& other) noexcept {
  if (this != &other) {
    if (!moved_from_ && gui_) gui_->PopStyleVar();
    gui_ = other.gui_;
    moved_from_ = false;
    other.moved_from_ = true;
  }
  return *this;
}

}  // namespace zebes
