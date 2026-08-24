#include "editor/image_generation/image_generation_lifecycle_panel.h"

#include <cstddef>
#include <string>

#include "absl/strings/str_cat.h"
#include "editor/gui_interface.h"
#include "editor/image_generation/image_generation_request_controller.h"
#include "editor/imgui_scoped.h"

namespace zebes {

ImageGenerationUiState BuildImageGenerationUiState(
    const ImageGenerationRequestController& controller) {
  ImageGenerationUiState state;
  state.selected_provider = controller.selected_provider();
  state.capabilities = controller.capabilities();
  state.in_flight = controller.in_flight();
  state.review = controller.review().has_value() ? &*controller.review() : nullptr;
  state.selected_candidate = state.review == nullptr ? 0 : state.review->selected;
  state.providers.reserve(controller.providers().size());
  for (const ImageGenerationProvider& provider : controller.providers()) {
    state.providers.push_back({
        .name = provider.name,
        .available = provider.available(),
        .unavailable_reason = provider.unavailable_reason,
    });
  }
  return state;
}

ImageGenerationLifecycleResult RenderImageGenerationLifecycle(
    GuiInterface& gui, const ImageGenerationLifecyclePanelOptions& options,
    ImageGenerationUiState& state) {
  ImageGenerationLifecycleResult result;
  const ImageGenerationProviderStatus* selected = state.selected_provider < state.providers.size()
                                                      ? &state.providers[state.selected_provider]
                                                      : nullptr;
  const char* provider_preview = selected == nullptr ? "(unavailable)" : selected->name.c_str();
  const std::string provider_id = absl::StrCat("Provider##", options.editor_id, "Generation");
  gui.SetNextItemWidth(options.provider_width);
  gui.BeginDisabled(state.in_flight);
  {
    ScopedCombo combo = gui.CreateScopedCombo(provider_id.c_str(), provider_preview);
    if (combo.IsActive()) {
      for (size_t index = 0; index < state.providers.size(); ++index) {
        const ImageGenerationProviderStatus& provider = state.providers[index];
        const ImGuiSelectableFlags flags = provider.available ? 0 : ImGuiSelectableFlags_Disabled;
        if (!gui.Selectable(provider.name.c_str(), index == state.selected_provider, flags)) {
          continue;
        }
        state.selected_provider = index;
        result.action = ImageGenerationLifecycleAction::kSelectProvider;
      }
    }
  }
  gui.EndDisabled();

  if (state.in_flight) {
    gui.TextWrapped("Generating. This can take a minute.");
    const std::string cancel_id = absl::StrCat("Cancel generation##", options.editor_id);
    if (gui.Button(cancel_id.c_str())) result.action = ImageGenerationLifecycleAction::kCancel;
    return result;
  }
  if (state.review == nullptr) {
    result.show_draft = true;
    return result;
  }

  const ImageGenerationReview& review = *state.review;
  const size_t count = review.candidates.size();
  gui.Text("Candidate %zu of %zu", review.selected + 1, count);
  if (count > 1) {
    const std::string previous_id = absl::StrCat("Previous##", options.editor_id, "Candidate");
    if (gui.Button(previous_id.c_str())) {
      state.selected_candidate = review.selected == 0 ? count - 1 : review.selected - 1;
      result.action = ImageGenerationLifecycleAction::kSelectCandidate;
    }
    gui.SameLine();
    const std::string next_id = absl::StrCat("Next##", options.editor_id, "Candidate");
    if (gui.Button(next_id.c_str())) {
      state.selected_candidate = review.selected + 1 == count ? 0 : review.selected + 1;
      result.action = ImageGenerationLifecycleAction::kSelectCandidate;
    }
  }
  const ImageGenerationCandidate& candidate = review.candidates[review.selected];
  if (candidate.revised_prompt.has_value()) {
    gui.TextWrapped("Provider rewrote the prompt: %s", candidate.revised_prompt->c_str());
  }
  gui.TextDisabled("%s / %s", review.provider.c_str(), review.model.c_str());

  gui.BeginDisabled(!options.can_accept_candidate);
  const std::string accept_id = absl::StrCat("Accept candidate##", options.editor_id);
  if (gui.Button(accept_id.c_str())) {
    result.action = ImageGenerationLifecycleAction::kAcceptCandidate;
  }
  gui.EndDisabled();
  gui.SameLine();
  const std::string discard_id = absl::StrCat("Discard##", options.editor_id, "Candidate");
  if (gui.Button(discard_id.c_str())) {
    result.action = ImageGenerationLifecycleAction::kDiscardCandidates;
  }
  if (!options.can_accept_candidate && options.acceptance_blocked_message != nullptr) {
    gui.TextWrapped("%s", options.acceptance_blocked_message);
  }
  return result;
}

}  // namespace zebes
