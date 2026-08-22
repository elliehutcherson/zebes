#pragma once

#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"

namespace zebes {

// Starts a visually consistent inspector section. Descriptions should explain
// authored meaning or units rather than repeat the heading.
void RenderInspectorSection(GuiInterface& gui, const char* title,
                            const char* description = nullptr);

// Two-column inspector layout with permanent labels and full-width controls.
// Widget labels inside a row should be hidden ImGui IDs (for example
// "##level_name"); the visible, user-facing label belongs to BeginRow.
class InspectorPropertyGrid {
 public:
  InspectorPropertyGrid(GuiInterface& gui, const char* id, float label_width = 128.0f);

  explicit operator bool() const { return static_cast<bool>(table_); }

  // Advances to a row and prepares the value column for a full-width control.
  // When help is supplied, hovering the permanent label explains the field.
  bool BeginRow(const char* label, const char* help = nullptr);

 private:
  GuiInterface& gui_;
  ScopedTable table_;
};

}  // namespace zebes
