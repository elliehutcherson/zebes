#pragma once

#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "editor/gui_interface.h"

namespace zebes {

// A destructive button that asks before it acts.
//
// Deleting a tileset, a level, a sprite or a blueprint used to happen on one
// click with nothing in between. This replaces that button with a question the
// moment it is pressed, and only acts once the user answers.
//
// Asking in place rather than through a modal is deliberate: GuiInterface has
// no OpenPopup/BeginPopupModal, and growing the interface, Gui and MockGui for
// one dialog buys nothing an inline question does not. It also keeps the whole
// interaction drivable from MockGui in a panel test.
//
// The armed target is remembered rather than just a bool, because a
// confirmation belongs to the thing it was raised against. Without that, moving
// the selection while a Confirm is on screen would leave it primed to destroy
// whatever is selected now.
class ConfirmPrompt {
 public:
  // Draws either the destructive button or the question that replaces it, and
  // returns true on the single frame the user confirms.
  //
  //   label      text of the destructive button, e.g. "Delete".
  //   target     identifies what would be destroyed. Rendering with a different
  //              target than the one armed disarms the prompt, so a stale
  //              question cannot be answered against a new selection.
  //   question   shown above the Confirm/Cancel pair. Empty renders the compact
  //              inline form, for a button that sits on a list row.
  //   id_suffix  ImGui "##" id disambiguator, so several prompts can share one
  //              panel. Must be unique within the panel.
  bool Render(GuiInterface& gui, absl::string_view label, absl::string_view target,
              absl::string_view question, absl::string_view id_suffix);

  // Raises the question without the prompt's own button having been pressed.
  //
  // Some destructive actions are only destructive sometimes: closing an editor
  // matters when there are unsaved edits and not otherwise. Those callers own
  // the button, decide whether it needs asking, and arm the prompt themselves.
  void Arm(absl::string_view target) { armed_target_ = std::string(target); }

  // Whether a question is currently on screen. A caller that lays out other
  // controls around the prompt needs to know it has grown a row.
  bool armed() const { return armed_target_.has_value(); }

  // Drops any pending question. Call when the user does something other than
  // answer it, so the prompt never outlives the intent that raised it.
  void Disarm() { armed_target_.reset(); }

 private:
  std::optional<std::string> armed_target_;
};

}  // namespace zebes
