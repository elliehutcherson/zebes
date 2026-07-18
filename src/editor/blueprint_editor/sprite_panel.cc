#include "editor/blueprint_editor/sprite_panel.h"

#include "SDL_render.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/editor_utils.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SpritePanel>> SpritePanel::Create(Api* api, GuiInterface* gui) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("API can not be null.");
  }
  if (gui == nullptr) {
    return absl::InvalidArgumentError("GUI can not be null.");
  }
  return absl::WrapUnique(new SpritePanel(api, gui));
}

SpritePanel::SpritePanel(Api* api, GuiInterface* gui) : api_(*api), gui_(gui) {
  RefreshSpriteCache();
}

void SpritePanel::RefreshSpriteCache() { model_.SetSprites(api_.GetAllSprites()); }

absl::Status SpritePanel::Attach(const std::string& id) {
  Detach();
  ASSIGN_OR_RETURN(Sprite * sprite, api_.GetSprite(id));
  model_.AttachSprite(*sprite);
  canvas_sprite_ = std::make_unique<CanvasSprite>(*model_.editing_sprite());

  return absl::OkStatus();
}

absl::Status SpritePanel::Attach(int i) {
  model_.SelectSpriteIndex(i);
  RETURN_IF_ERROR(model_.AttachSelectedSprite());
  canvas_sprite_ = std::make_unique<CanvasSprite>(*model_.editing_sprite());

  return absl::OkStatus();
}

void SpritePanel::Detach() {
  model_.DetachSprite();
  canvas_sprite_.reset();
}

absl::StatusOr<SpriteResult> SpritePanel::Render() {
  ScopedId id = gui_->CreateScopedId("SpritePanel");

  if (model_.has_editing_sprite()) {
    return RenderDetails();
  }
  return RenderList();
}

absl::StatusOr<SpriteResult> SpritePanel::RenderList() {
  counters_.render_list++;
  SpriteResult result;

  const float button_width = CalculateButtonWidth(gui_, /*num_buttons=*/2);
  if (gui_->Button("Refresh", ImVec2(button_width, 0))) {
    RefreshSpriteCache();
  }

  // Attach button
  gui_->SameLine();
  if (gui_->Button("Attach", ImVec2(-FLT_MIN, 0)) && model_.selected_sprite_index() > -1) {
    RETURN_IF_ERROR(Attach(model_.selected_sprite_index()));
    result = {SpriteResult::Type::kAttach, model_.editing_sprite()->id};
  }
  if (auto list_box = gui_->CreateScopedListBox("Sprites", ImVec2(-FLT_MIN, -FLT_MIN)); list_box) {
    for (int i = 0; i < static_cast<int>(model_.sprites().size()); ++i) {
      const bool is_selected = (model_.selected_sprite_index() == i);
      if (gui_->Selectable(model_.sprites()[i].name_id().c_str(), is_selected)) {
        model_.SelectSpriteIndex(i);
      }

      if (is_selected) gui_->SetItemDefaultFocus();
    }
  }

  return result;
}

absl::StatusOr<SpriteResult> SpritePanel::RenderDetails() {
  counters_.render_details++;
  SpriteResult result;
  Sprite* editing_sprite = model_.editing_sprite();

  gui_->Text("ID: %s", editing_sprite->id.c_str());
  gui_->Text("Name: %s", editing_sprite->name.c_str());

  gui_->Separator();

  if (editing_sprite->frames.empty()) {
    gui_->Text("No frames available.");
  } else {
    // Slider for frame selection
    int frame_index = model_.frame_index();
    if (gui_->SliderInt("Frame", &frame_index, /*v_min=*/0,
                        /*v_max=*/editing_sprite->frames.size() - 1)) {
      RETURN_IF_ERROR(model_.SetFrameIndex(frame_index));
    }

    RenderFrameDetails();
  }

  gui_->Separator();

  // Action Buttons
  float button_width = CalculateButtonWidth(gui_, /*num_buttons=*/2);

  if (gui_->Button("Save", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(ConfirmState());
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
    if (gui_->Button("Detach", ImVec2(button_width, 0))) {
      Detach();
      result.type = SpriteResult::Type::kDetach;
    }
  }

  return result;
}

absl::StatusOr<bool> SpritePanel::RenderCanvas(Canvas& canvas, bool input_allowed) {
  if (!model_.has_editing_sprite()) return false;

  return canvas_sprite_->Render(canvas, model_.frame_index(), input_allowed);
}

absl::Status SpritePanel::ConfirmState() {
  RETURN_IF_ERROR(api_.UpdateSprite(*model_.editing_sprite()));
  RefreshSpriteCache();

  LOG(INFO) << "Saved sprite: " << model_.editing_sprite()->name;
  return absl::OkStatus();
}

void SpritePanel::RenderFrameDetails() {
  Sprite* editing_sprite = model_.editing_sprite();
  if (!editing_sprite->texture_handle) {
    gui_->TextColored(ImVec4(1, 0, 0, 1), "Texture not loaded");
    return;
  }

  SpriteFrame* frame = model_.current_frame();
  if (frame == nullptr) return;

  int tex_w, tex_h;
  SDL_Texture* texture = SdlTextureHandleAdapter::ToNative(editing_sprite->texture_handle);
  if (texture == nullptr) {
    gui_->TextColored(ImVec4(1, 0, 0, 1), "Texture resource unavailable");
    return;
  }
  SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);

  float avail_width = gui_->GetContentRegionAvail().x;
  absl::StatusOr<SpriteFramePreview> preview =
      SpritePanelModel::CalculateFramePreview(*frame, tex_w, tex_h, avail_width);
  if (!preview.ok()) return;

  ImVec2 size(preview->width, preview->height);
  ImVec2 uv0(preview->uv0_x, preview->uv0_y);
  ImVec2 uv1(preview->uv1_x, preview->uv1_y);
  gui_->Image(reinterpret_cast<ImTextureID>(texture), size, uv0, uv1);

  gui_->Separator();
  gui_->Text("Frame Details");

  // Editable fields
  gui_->InputInt("Offset X", &frame->offset_x);
  gui_->InputInt("Offset Y", &frame->offset_y);

  // Read-only fields
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(true);
    gui_->InputInt("Index", &frame->index);
    gui_->InputInt("Texture X", &frame->texture_x);
    gui_->InputInt("Texture Y", &frame->texture_y);
    gui_->InputInt("Texture W", &frame->texture_w);
    gui_->InputInt("Texture H", &frame->texture_h);
    gui_->InputInt("Render W", &frame->render_w);
    gui_->InputInt("Render H", &frame->render_h);
    gui_->InputInt("Frames Per Cycle", &frame->frames_per_cycle);
  }
}

}  // namespace zebes
