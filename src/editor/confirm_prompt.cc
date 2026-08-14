#include "editor/confirm_prompt.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {
namespace {

// Every destructive control in the editor wears the same red, so a question
// reads as belonging to the button that raised it.
constexpr ImVec4 kDestructiveColor{0.8f, 0.2f, 0.2f, 1.0f};

}  // namespace

bool ConfirmPrompt::Render(GuiInterface& gui, absl::string_view label, absl::string_view target,
                           absl::string_view question, absl::string_view id_suffix) {
  // A question raised against one object must not be answerable against
  // another. Re-rendering with a different target is how the panel says the
  // selection moved on.
  if (armed_target_.has_value() && *armed_target_ != target) Disarm();

  if (!armed_target_.has_value()) {
    ScopedStyleColor style = gui.CreateScopedStyleColor(ImGuiCol_Button, kDestructiveColor);
    if (gui.Button(absl::StrCat(label, "##", id_suffix).c_str())) {
      armed_target_ = std::string(target);
    }
    return false;
  }

  if (!question.empty()) gui.TextWrapped("%s", std::string(question).c_str());

  {
    ScopedStyleColor style = gui.CreateScopedStyleColor(ImGuiCol_Button, kDestructiveColor);
    if (gui.Button(absl::StrCat("Confirm##", id_suffix).c_str())) {
      Disarm();
      return true;
    }
  }
  gui.SameLine();
  if (gui.Button(absl::StrCat("Cancel##", id_suffix).c_str())) Disarm();
  return false;
}

}  // namespace zebes
