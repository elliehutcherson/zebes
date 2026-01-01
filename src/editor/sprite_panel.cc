#include "editor/sprite_panel.h"

#include <algorithm>

#include "SDL_render.h"
#include "absl/cleanup/cleanup.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "editor/editor_utils.h"
#include "imgui.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<SpritePanel>> SpritePanel::Create(Api* api) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("API can not be null.");
  }
  return absl::WrapUnique(new SpritePanel(api));
}

SpritePanel::SpritePanel(Api* api) : api_(api) { RefreshSpriteCache(); }

void SpritePanel::RefreshSpriteCache() {
  sprite_cache_ = api_->GetAllSprites();

  std::sort(sprite_cache_.begin(), sprite_cache_.end(),
            [](const Sprite& a, const Sprite& b) { return a.name < b.name; });
}

Sprite* SpritePanel::GetSprite() {
  if (editting_sprite_.has_value()) {
    return &(*editting_sprite_);
  }
  if (sprite_index_ > -1 && sprite_index_ < sprite_cache_.size()) {
    return &sprite_cache_[sprite_index_];
  }
  return nullptr;
}

int SpritePanel::GetFrameIndex() const { return current_frame_index_; }

void SpritePanel::SetSprite(const std::string& id) {
  for (int i = 0; i < sprite_cache_.size(); ++i) {
    if (sprite_cache_[i].id != id) continue;

    sprite_index_ = i;
    editting_sprite_ = sprite_cache_[i];
    current_frame_index_ = 0;
    return;
  }
  // If not found, deselect
  sprite_index_ = -1;
  editting_sprite_ = std::nullopt;
  LOG(ERROR) << "No sprite found!!";
}

void SpritePanel::SetAttachedSprite(const std::optional<std::string>& id) {
  attached_sprite_id_ = id;
}

bool SpritePanel::IsAttached() const {
  if (!editting_sprite_.has_value() || !attached_sprite_id_.has_value()) {
    return false;
  }
  return editting_sprite_->id == *attached_sprite_id_;
}

void SpritePanel::Reset() {
  sprite_index_ = -1;
  editting_sprite_ = std::nullopt;
  attached_sprite_id_ = std::nullopt;
  current_frame_index_ = 0;
}

SpriteResult SpritePanel::Render() {
  ImGui::PushID("SpritePanel");
  auto cleanup = absl::MakeCleanup([] { ImGui::PopID(); });

  if (editting_sprite_.has_value()) {
    return RenderDetails();
  }
  return RenderList();
}

SpriteResult SpritePanel::RenderList() {
  SpriteResult result;

  const int num_buttons = (sprite_index_ > -1) ? 2 : 1;
  const float button_width = CalculateButtonWidth(num_buttons);

  if (ImGui::Button("Refresh", ImVec2(button_width, 0))) {
    RefreshSpriteCache();
  }

  // Attach button
  if (sprite_index_ > -1) {
    ImGui::SameLine();
    if (ImGui::Button("Attach", ImVec2(-FLT_MIN, 0))) {
      editting_sprite_ = sprite_cache_[sprite_index_];
      current_frame_index_ = 0;
      attached_sprite_id_ = editting_sprite_->id;
      result.type = SpriteResult::Type::kAttach;
      result.id = *attached_sprite_id_;
    }
  }

  if (ImGui::BeginListBox("Sprites", ImVec2(-FLT_MIN, -FLT_MIN))) {
    for (int i = 0; i < sprite_cache_.size(); ++i) {
      const bool is_selected = (sprite_index_ == i);
      if (ImGui::Selectable(sprite_cache_[i].name.c_str(), is_selected)) {
        sprite_index_ = i;
      }

      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndListBox();
  }

  return result;
}

SpriteResult SpritePanel::RenderDetails() {
  SpriteResult result;

  ImGui::Text("ID: %s", editting_sprite_->id.c_str());
  ImGui::Text("Name: %s", editting_sprite_->name.c_str());

  ImGui::Separator();

  if (editting_sprite_->frames.empty()) {
    ImGui::Text("No frames available.");
  } else {
    // Slider for frame selection
    if (editting_sprite_->frames.size() > 1) {
      ImGui::SliderInt("Frame", &current_frame_index_, 0,
                       static_cast<int>(editting_sprite_->frames.size()) - 1);
    }

    if (current_frame_index_ >= 0 && current_frame_index_ < editting_sprite_->frames.size()) {
      RenderFrameDetails(current_frame_index_);
    }
  }

  ImGui::Separator();

  // Action Buttons
  float button_width = CalculateButtonWidth(/*num_buttons=*/2);

  if (ImGui::Button("Save", ImVec2(button_width, 0))) {
    ConfirmState();
  }
  ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
  if (ImGui::Button("Detach", ImVec2(button_width, 0))) {
    result.type = SpriteResult::Type::kDetach;
    editting_sprite_ = std::nullopt;
    attached_sprite_id_ = std::nullopt;
  }
  ImGui::PopStyleColor();

  return result;
}

void SpritePanel::ConfirmState() {
  if (!editting_sprite_.has_value()) {
    return;
  }

  absl::Status status = api_->UpdateSprite(*editting_sprite_);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to save sprite: " << status.message();
    return;
  }

  LOG(INFO) << "Saved sprite: " << editting_sprite_->name;
  RefreshSpriteCache();
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
