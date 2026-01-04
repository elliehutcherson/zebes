#include "editor/blueprint/sprite_panel.h"

#include <algorithm>

#include "SDL_render.h"
#include "absl/cleanup/cleanup.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "editor/editor_utils.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SpritePanel>> SpritePanel::Create(Api* api) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("API can not be null.");
  }
  return absl::WrapUnique(new SpritePanel(api));
}

SpritePanel::SpritePanel(Api* api) : api_(*api) { RefreshSpriteCache(); }

void SpritePanel::RefreshSpriteCache() {
  sprite_cache_ = api_.GetAllSprites();

  std::sort(sprite_cache_.begin(), sprite_cache_.end(),
            [](const Sprite& a, const Sprite& b) { return a.name < b.name; });
}

absl::Status SpritePanel::Attach(const std::string& id) {
  Detach();
  ASSIGN_OR_RETURN(Sprite * sprite, api_.GetSprite(id));
  editting_sprite_ = *sprite;
  canvas_sprite_ = std::make_unique<CanvasSprite>(*editting_sprite_);

  return absl::OkStatus();
}

absl::Status SpritePanel::Attach(int i) {
  Detach();
  if (i < 0 || i >= sprite_cache_.size()) {
    return absl::OutOfRangeError("Cannot attach sprite, index out of range!!!");
  }
  editting_sprite_ = sprite_cache_[i];
  canvas_sprite_ = std::make_unique<CanvasSprite>(*editting_sprite_);

  return absl::OkStatus();
}

void SpritePanel::Detach() {
  frame_index_ = 0;
  sprite_index_ = -1;
  editting_sprite_.reset();
  canvas_sprite_.reset();
}

absl::StatusOr<SpriteResult> SpritePanel::Render() {
  ImGui::PushID("SpritePanel");
  auto cleanup = absl::MakeCleanup([] { ImGui::PopID(); });

  if (editting_sprite_.has_value()) {
    return RenderDetails();
  }
  return RenderList();
}

absl::StatusOr<SpriteResult> SpritePanel::RenderList() {
  counters_.render_list++;
  SpriteResult result;

  const float button_width = CalculateButtonWidth(/*num_buttons=*/2);
  if (ImGui::Button("Refresh", ImVec2(button_width, 0))) {
    RefreshSpriteCache();
  }

  // Attach button
  ImGui::SameLine();
  if (ImGui::Button("Attach", ImVec2(-FLT_MIN, 0)) && sprite_index_ > -1) {
    RETURN_IF_ERROR(Attach(sprite_index_));
    result = {SpriteResult::Type::kAttach, editting_sprite_->id};
  }

  if (ImGui::BeginListBox("Sprites", ImVec2(-FLT_MIN, -FLT_MIN))) {
    for (int i = 0; i < sprite_cache_.size(); ++i) {
      const bool is_selected = (sprite_index_ == i);
      if (ImGui::Selectable(sprite_cache_[i].name_id().c_str(), is_selected)) {
        sprite_index_ = i;
      }

      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndListBox();
  }

  return result;
}

absl::StatusOr<SpriteResult> SpritePanel::RenderDetails() {
  counters_.render_details++;
  SpriteResult result;

  ImGui::Text("ID: %s", editting_sprite_->id.c_str());
  ImGui::Text("Name: %s", editting_sprite_->name.c_str());

  ImGui::Separator();

  if (editting_sprite_->frames.empty()) {
    ImGui::Text("No frames available.");
  } else {
    // Slider for frame selection
    ImGui::SliderInt("Frame", &frame_index_, /*v_min=*/0,
                     /*v_max=*/editting_sprite_->frames.size() - 1);

    RenderFrameDetails(frame_index_);
  }

  ImGui::Separator();

  // Action Buttons
  float button_width = CalculateButtonWidth(/*num_buttons=*/2);

  if (ImGui::Button("Save", ImVec2(button_width, 0))) {
    RETURN_IF_ERROR(ConfirmState());
  }
  ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
  if (ImGui::Button("Detach", ImVec2(button_width, 0))) {
    Detach();
    result.type = SpriteResult::Type::kDetach;
  }
  ImGui::PopStyleColor();

  return result;
}

absl::StatusOr<bool> SpritePanel::RenderCanvas(Canvas& canvas, bool input_allowed) {
  if (!editting_sprite_.has_value()) return false;

  return canvas_sprite_->Render(canvas, frame_index_, input_allowed);
}

absl::Status SpritePanel::ConfirmState() {
  RETURN_IF_ERROR(api_.UpdateSprite(*editting_sprite_));
  RefreshSpriteCache();

  LOG(INFO) << "Saved sprite: " << editting_sprite_->name;
  return absl::OkStatus();
}

std::pair<ImVec2, ImVec2> SpritePanel::GetFrameUVs(const SpriteFrame& frame, int tex_w,
                                                   int tex_h) const {
  ImVec2 uv0((float)frame.texture_x / tex_w, (float)frame.texture_y / tex_h);
  ImVec2 uv1((float)(frame.texture_x + frame.texture_w) / tex_w,
             (float)(frame.texture_y + frame.texture_h) / tex_h);
  return {uv0, uv1};
}

void SpritePanel::RenderFrameDetails(int frame_index) {
  if (!editting_sprite_->sdl_texture) {
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Texture not loaded");
    return;
  }

  SpriteFrame& frame = editting_sprite_->frames[frame_index];
  int tex_w, tex_h;
  SDL_QueryTexture((SDL_Texture*)editting_sprite_->sdl_texture, nullptr, nullptr, &tex_w, &tex_h);

  if (tex_w <= 0 || tex_h <= 0) return;

  std::pair<ImVec2, ImVec2> uvs = GetFrameUVs(frame, tex_w, tex_h);

  float avail_width = ImGui::GetContentRegionAvail().x;
  float scale = 1.0f;
  if (frame.texture_w > avail_width) {
    scale = avail_width / frame.texture_w;
  }

  ImVec2 size((float)frame.texture_w * scale, (float)frame.texture_h * scale);
  ImGui::Image((ImTextureID)editting_sprite_->sdl_texture, size, uvs.first, uvs.second);

  ImGui::Separator();
  ImGui::Text("Frame Details");

  // Editable fields
  ImGui::InputInt("Offset X", &frame.offset_x);
  ImGui::InputInt("Offset Y", &frame.offset_y);

  // Read-only fields
  ImGui::BeginDisabled();
  ImGui::InputInt("Index", &frame.index);
  ImGui::InputInt("Texture X", &frame.texture_x);
  ImGui::InputInt("Texture Y", &frame.texture_y);
  ImGui::InputInt("Texture W", &frame.texture_w);
  ImGui::InputInt("Texture H", &frame.texture_h);
  ImGui::InputInt("Render W", &frame.render_w);
  ImGui::InputInt("Render H", &frame.render_h);
  ImGui::InputInt("Frames Per Cycle", &frame.frames_per_cycle);
  ImGui::EndDisabled();
}

}  // namespace zebes