#include "editor/prop_artwork_editor/prop_artwork_output_panel.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "editor/imgui_scoped.h"

namespace zebes {
namespace {

constexpr float kFieldWidth = 200.0f;

const char* PreviewPolicyLabel(PropPreviewPolicy policy) {
  return policy == PropPreviewPolicy::kReviewEachStage ? "Review each step" : "Finished only";
}

}  // namespace

absl::StatusOr<std::unique_ptr<PropArtworkOutputPanel>> PropArtworkOutputPanel::Create(
    GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("Prop artwork output requires a GUI");
  return absl::WrapUnique(new PropArtworkOutputPanel(gui));
}

PropArtworkOutputPanel::Action PropArtworkOutputPanel::RenderRecipeSelector(
    PropArtworkEditorModel& model, const std::vector<PropRecipe>& recipes) {
  const char* preview =
      model.active_recipe().has_value() ? model.active_recipe()->name.c_str() : "New prop";
  ScopedCombo combo = gui_->CreateScopedCombo("Recipe##PropArtworkOut", preview);
  if (!combo.IsActive()) return Action::kNone;
  for (const PropRecipe& recipe : recipes) {
    const bool selected =
        model.active_recipe().has_value() && model.active_recipe()->id == recipe.id;
    if (!gui_->Selectable(recipe.name.c_str(), selected)) continue;
    model.recipe_to_open() = recipe.id;
    return Action::kOpenRecipe;
  }
  return Action::kNone;
}

bool PropArtworkOutputPanel::RenderDeleteControl(const PropArtworkEditorModel& model) {
  const PropRecipe& recipe = *model.active_recipe();
  const std::string question = absl::StrCat(
      "Delete prop '", recipe.name, "', its blueprint, sprite, texture, and unshared source?");
  return delete_prompt_.Render(*gui_, "Delete", recipe.id, question, "PropArtworkOut");
}

void PropArtworkOutputPanel::RenderPreviewPolicy(PropArtworkEditorModel& model) {
  const PropPreviewPolicy current = model.preview_policy();
  ScopedCombo combo =
      gui_->CreateScopedCombo("Preview##PropArtworkOut", PreviewPolicyLabel(current));
  if (!combo.IsActive()) return;
  for (const PropPreviewPolicy policy :
       {PropPreviewPolicy::kFinishedOnly, PropPreviewPolicy::kReviewEachStage}) {
    if (!gui_->Selectable(PreviewPolicyLabel(policy), policy == current)) continue;
    model.SetPreviewPolicy(policy);
  }
}

PropArtworkOutputPanel::Action PropArtworkOutputPanel::Render(
    PropArtworkEditorModel& model, const std::vector<PropRecipe>& recipes, bool work_in_progress) {
  gui_->Text("Output");
  Action action = RenderRecipeSelector(model, recipes);
  if (model.active_recipe().has_value()) {
    if (gui_->Button("Save As##PropArtworkOut")) action = Action::kCopyRecipe;
    if (RenderDeleteControl(model)) action = Action::kDelete;
  }

  const std::string clear_target =
      model.active_recipe().has_value()
          ? absl::StrCat("recipe:", model.active_recipe()->id)
          : absl::StrCat("source:", model.source().has_value() ? model.source()->id : "none");
  constexpr char kClearQuestion[] =
      "Clear the Prop Artwork workspace? Unsaved settings and an uncommitted imported source "
      "will be discarded. Saved prop bundles are not deleted.";
  if (clear_prompt_.Render(*gui_, "Clear workspace", clear_target, kClearQuestion,
                           "PropArtworkClear")) {
    action = Action::kClearWorkspace;
  }

  gui_->SetNextItemWidth(kFieldWidth);
  if (model.active_recipe().has_value()) gui_->BeginDisabled();
  const bool name_changed = gui_->InputText("Name##PropArtworkOut", &model.name());
  if (model.active_recipe().has_value()) gui_->EndDisabled();
  if (name_changed) model.MarkInputsChanged();

  RenderPreviewPolicy(model);
  gui_->Separator();

  const bool cannot_prepare = work_in_progress || !model.CanPrepare();
  gui_->BeginDisabled(cannot_prepare);
  if (gui_->Button(model.HasPreparedResult() ? "Reprocess##PropArtworkOut"
                                             : "Process##PropArtworkOut")) {
    action = Action::kPrepare;
  }
  gui_->EndDisabled();

  gui_->BeginDisabled(work_in_progress || !model.HasPreparedResult());
  const char* commit_label = model.active_recipe().has_value()
                                 ? "Apply regeneration##PropArtworkOut"
                                 : "Create prop##PropArtworkOut";
  if (gui_->Button(commit_label)) action = Action::kCommit;
  gui_->EndDisabled();

  if (model.name().empty()) gui_->TextWrapped("Name the prop first.");
  if (!model.source().has_value()) gui_->TextWrapped("Choose or import a retained source.");
  if (!model.has_style()) gui_->TextWrapped("Choose a terrain style.");
  if (!model.status().empty()) {
    gui_->TextWrapped("%s", model.status().c_str());
    if (gui_->Button("Dismiss status##PropArtworkOut")) model.ClearStatus();
  }
  return action;
}

}  // namespace zebes
