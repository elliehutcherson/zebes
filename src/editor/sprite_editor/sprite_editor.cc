#include "editor/sprite_editor/sprite_editor.h"

#include "SDL_render.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/sdl_wrapper.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"
#include "objects/level.h"

namespace zebes {

constexpr float kSpriteListHeight = 300.0f;

absl::StatusOr<std::unique_ptr<SpriteEditor>> SpriteEditor::Create(Api* api, SdlWrapper* sdl,
                                                                   GuiInterface* gui) {
  if (api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null");
  }
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SdlWrapper must not be null");
  }
  if (gui == nullptr) {
    return absl::InvalidArgumentError("GUI must not be null");
  }
  return std::unique_ptr<SpriteEditor>(new SpriteEditor(api, sdl, gui));
}

SpriteEditor::SpriteEditor(Api* api, SdlWrapper* sdl, GuiInterface* gui)
    : api_(api), sdl_(sdl), gui_(gui) {
  RefreshSpriteList();
}

void SpriteEditor::LoadSpriteTexture(const std::string& texture_id) {
  absl::StatusOr<Texture*> texture = api_->GetTexture(texture_id);
  if (!texture.ok()) {
    sprite_.sdl_texture = nullptr;
    LOG(ERROR) << "Failed to load sprite texture: " << texture.status();
    return;
  }

  sprite_.sdl_texture = (*texture)->sdl_texture;
}

void SpriteEditor::RefreshSpriteList() {
  std::vector<Sprite> sprites = api_->GetAllSprites();

  std::sort(sprites.begin(), sprites.end(),
            [](const Sprite& a, const Sprite& b) { return a.name < b.name; });

  sprite_list_ = sprites;
  LOG(INFO) << "Loaded " << sprite_list_.size() << " sprites.";

  absl::StatusOr<std::vector<Texture>> textures = api_->GetAllTextures();
  if (!textures.ok()) {
    LOG(ERROR) << "Failed to load textures: " << textures.status();
    texture_list_.clear();
  } else {
    texture_list_ = *textures;
    LOG(INFO) << "Loaded " << texture_list_.size() << " textures.";
  }
}

void SpriteEditor::SelectSprite(const std::string& sprite_id) {
  new_sprite_ = false;

  // Find config and setup editing buffer
  bool found = false;
  for (const Sprite& s : sprite_list_) {
    if (s.id != sprite_id) continue;

    sprite_ = s;
    edit_name_buffer_ = s.name;
    // Ensure buffer has enough space for editing
    if (edit_name_buffer_.size() < 256) {
      edit_name_buffer_.resize(256, '\0');
    }
    // Deep copy frames
    original_frames_ = s.frames;
    found = true;
    break;
  }

  if (!found) {
    LOG(ERROR) << "Selected sprite not found in list: " << sprite_id;
    return;
  }
  LoadSpriteTexture(sprite_.texture_id);

  active_frame_index_ = -1;  // Reset active selection

  // Reset animator
  animator_->SetSprite(sprite_);  // Animator also needs update potentially?
  is_playing_animation_ = false;
  animation_timer_ = 0.0;
}

// Below the methods for upserting, updating and deleting sprites are defined.
void SpriteEditor::UpsertSprite(const std::string& sprite_id) {
  sprite_.name = edit_name_buffer_.c_str();
  if (sprite_.texture_id.empty()) {
    LOG(ERROR) << "Texture must be selected.";
    return;
  }

  // Create new sprite
  absl::StatusOr<std::string> new_id = api_->CreateSprite(sprite_);
  if (!new_id.ok()) {
    LOG(ERROR) << "Failed to create sprite: " << new_id.status();
    return;
  }

  LOG(INFO) << "Created new sprite: " << *new_id;
  new_sprite_ = false;
  RefreshSpriteList();
  SelectSprite(*new_id);
};

void SpriteEditor::UpdateSprite(const std::string& sprite_id) {
  // Update config from buffer
  sprite_.name = edit_name_buffer_.c_str();

  // Ensure frames indices are correct
  for (int i = 0; i < sprite_.frames.size(); ++i) {
    sprite_.frames[i].index = i;
  }

  absl::Status status = api_->UpdateSprite(sprite_);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to update sprite: " << status;
    return;
  }

  LOG(INFO) << "Updated sprite config.";
  original_frames_ = sprite_.frames;
  RefreshSpriteList();  // Refresh list to show new name
};

void SpriteEditor::DeleteSprite(const std::string& sprite_id) {
  absl::Status status = api_->DeleteSprite(sprite_id);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to delete sprite: " << status;
    return;
  }

  LOG(INFO) << "Deleted sprite " << sprite_id;
  sprite_ = {};
  original_frames_.clear();
  active_frame_index_ = -1;
  RefreshSpriteList();
};

void SpriteEditor::SaveSpriteFrames() { UpdateSprite(sprite_.id); }

void SpriteEditor::Render() {
  RenderSpriteSelection();
  RenderSpriteFrameList();
  RenderFullTextureView();
}

void SpriteEditor::RenderSpriteSelection() {
  // Use tables for list and inspector
  auto table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
  ScopedTable table(gui_, "SpriteListSplit", 2, table_flags);
  gui_->TableSetupColumn("Sprite List", ImGuiTableColumnFlags_WidthFixed, 250.0f);
  gui_->TableSetupColumn("Sprite Details", ImGuiTableColumnFlags_WidthStretch);

  gui_->TableNextRow();
  gui_->TableNextColumn();

  // Column 1: Sprite List
  RenderSpriteList();

  gui_->TableNextColumn();

  // Column 2: Inspector (Meta + Animation)
  RenderSpriteMeta();
  gui_->Separator();
  RenderSpriteAnimation();
}

void SpriteEditor::RenderSpriteList() {
  gui_->Text("Existing Sprites");

  // Auto-refresh once
  if (gui_->Button("Refresh Sprite List")) {
    RefreshSpriteList();
  }
  gui_->SameLine();
  if (gui_->Button("Create New Sprite")) {
    new_sprite_ = true;
    // Clear/Init buffers
    sprite_ = {};
    edit_name_buffer_ = "";
    edit_name_buffer_.resize(256, '\0');
    sprite_.frames.clear();
    original_frames_.clear();
    active_frame_index_ = -1;
  }

  // Create list with fixed height
  ScopedChild child(gui_, "##Sprites", ImVec2(0, kSpriteListHeight), false);
  for (const Sprite& sprite : sprite_list_) {
    std::string label = sprite.name_id();
    bool is_selected = (sprite_.id == sprite.id && !new_sprite_);

    if (gui_->Selectable(label.c_str(), is_selected)) {
      SelectSprite(sprite.id);
    }
    if (is_selected) {
      ImGui::SetItemDefaultFocus();
    }
  }
}

void SpriteEditor::RenderSpriteMeta() {
  if (sprite_.id.empty() && !new_sprite_) {
    gui_->Text("Select a sprite to edit or Create New.");
    return;
  }

  // Title
  std::string title = new_sprite_ ? "NewSprite" : absl::StrCat("Sprite: ", sprite_.id);
  gui_->Text("%s", title.c_str());
  gui_->Separator();

  // ID is auto-assigned
  {
    ScopedDisabled disabled(gui_, true);
    if (new_sprite_) {
      gui_->Text("ID: <Auto>");
    } else {
      // Read-only ID
      gui_->InputText("ID", const_cast<char*>(sprite_.id.c_str()), sprite_.id.size(),
                      ImGuiInputTextFlags_ReadOnly);
    }
  }

  // Texture Dropdown
  // Find current texture path from ID for display
  std::string current_tex_path = "Select Texture";
  if (!sprite_.texture_id.empty()) {
    for (const auto& tex : texture_list_) {
      if (tex.id == sprite_.texture_id) {
        current_tex_path = tex.path;
        break;
      }
    }
  }

  {
    ScopedDisabled disabled(gui_, !new_sprite_);
    ScopedCombo combo(gui_, "Texture", current_tex_path.c_str());
    for (const Texture& texture : texture_list_) {
      bool is_selected = (sprite_.texture_id == texture.id);

      if (gui_->Selectable(texture.path.c_str(), is_selected)) {
        sprite_.texture_id = texture.id;
        LoadSpriteTexture(sprite_.texture_id);
      }

      if (is_selected) ImGui::SetItemDefaultFocus();
    }
  }

  // Sprite Name
  if (gui_->InputText("Name", edit_name_buffer_.data(), edit_name_buffer_.size())) {
    // Handled by buffer
  }

  gui_->Spacing();
  gui_->Separator();
  gui_->Spacing();

  // Allow the user to create, update or delete sprite
  if (new_sprite_ && gui_->Button("Create Sprite")) {
    UpsertSprite(sprite_.id);
  } else if (!new_sprite_ && gui_->Button("Save Sprite Config")) {
    UpdateSprite(sprite_.id);
  }
  gui_->SameLine();

  {
    ScopedStyleColor style(gui_, ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Sprite")) {
      DeleteSprite(sprite_.id);
    }
  }
}

void SpriteEditor::RenderSpriteAnimation() {
  gui_->Text("Animation Preview");

  // Play/Pause Control
  if (gui_->Button(is_playing_animation_ ? "Pause" : "Play")) {
    is_playing_animation_ = !is_playing_animation_;
  }

  // Animation Logic
  if (is_playing_animation_) {
    // Assuming target FPS from config or default 60
    int target_fps = 60;
    double tick_duration = 1.0 / target_fps;

    animation_timer_ += ImGui::GetIO().DeltaTime;
    while (animation_timer_ >= tick_duration) {
      animator_->Update();
      animation_timer_ -= tick_duration;
    }
  }

  // Handle empty frames gracefully
  if (sprite_.frames.empty()) {
    gui_->TextDisabled("No frames to animate.");
    return;
  }

  // Render Animated Frame
  absl::StatusOr<SpriteFrame> frame = animator_->GetCurrentFrame();
  if (!frame.ok()) {
    if (is_playing_animation_) {
      gui_->Text("Animation Error: %s", frame.status().ToString().c_str());
    } else {
      gui_->Text("Press Play to start animation.");
    }
    return;
  }

  // Calculate UVs
  int tex_w = 0, tex_h = 0;
  if (sprite_.sdl_texture != nullptr) {
    SDL_QueryTexture(SdlTexture(), nullptr, nullptr, &tex_w, &tex_h);
  }

  if (tex_w > 0 && tex_h > 0) {
    ImVec2 uv0(static_cast<float>(frame->texture_x) / tex_w,
               static_cast<float>(frame->texture_y) / tex_h);
    ImVec2 uv1(static_cast<float>(frame->texture_x + frame->texture_w) / tex_w,
               static_cast<float>(frame->texture_y + frame->texture_h) / tex_h);

    gui_->Image(ImTextureId(), ImVec2(frame->render_w, frame->render_h), uv0, uv1);
  } else {
    gui_->Text("Invalid texture dimensions.");
  }

  gui_->Text("Frame Index: %d", frame->index);  // Debug info
}

void SpriteEditor::RenderSpriteFrameList() {
  if (sprite_.id.empty() && !new_sprite_) return;

  gui_->Separator();
  std::string header_text = new_sprite_ ? "Sprite Frames (New Sprite)"
                                        : absl::StrCat("Sprite Frames for ID: ", sprite_.id);
  gui_->Text("%s", header_text.c_str());

  // Controls
  if (gui_->Button("Add Frame")) {
    SpriteFrame new_frame;
    new_frame.texture_w = 32;  // Default size
    new_frame.texture_h = 32;
    new_frame.render_w = 32;
    new_frame.render_h = 32;
    new_frame.texture_x = 0;
    new_frame.texture_y = 0;
    sprite_.frames.push_back(new_frame);
    active_frame_index_ = static_cast<int>(sprite_.frames.size()) - 1;
  }
  if (!new_sprite_) {
    gui_->SameLine();
    if (gui_->Button("Save Changes")) {
      SaveSpriteFrames();
    }

    gui_->SameLine();
    if (gui_->Button("Reset Changes")) {
      sprite_.frames = original_frames_;
      active_frame_index_ = -1;
    }
  }

  // Horizontal scroll area
  float min_height = sprite_.frames.empty() ? 0.0f : 550.0f;
  ScopedChild child(gui_, "SpriteFramesList", ImVec2(0, min_height));

  if (sprite_.frames.empty()) {
    gui_->TextDisabled("No frames found.");
  } else {
    for (int i = 0; i < sprite_.frames.size(); ++i) {
      RenderSpriteFrameItem(i, sprite_.frames[i]);
      if (i >= sprite_.frames.size() - 1) break;

      gui_->SameLine();
      gui_->Dummy(ImVec2(10, 0));
      gui_->SameLine();
    }
  }
}

void SpriteEditor::RenderSpriteFrameItem(int index, SpriteFrame& frame) {
  float start_x = ImGui::GetCursorPosX();
  ScopedGroup group(gui_);

  ScopedId id(gui_, index);

  // Header / Active Toggle
  bool is_active = (active_frame_index_ == index);
  if (is_active) {
    ScopedStyleColor style(gui_, ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    if (gui_->Button(absl::StrCat("Active ##", index).c_str())) {
      active_frame_index_ = -1;  // Deselect
    }
  } else if (gui_->Button(absl::StrCat("Edit ##", index).c_str())) {
    active_frame_index_ = index;
  }

  gui_->SameLine();
  if (gui_->Button("X")) {
    // Delete this frame
    if (active_frame_index_ == index)
      active_frame_index_ = -1;
    else if (active_frame_index_ > index)
      active_frame_index_--;

    sprite_.frames.erase(sprite_.frames.begin() + index);

    return;  // Stop rendering this item (ScopedId/Group destructors called)
  }

  // Ordering buttons
  if (index > 0) {
    gui_->SameLine();
    if (gui_->ArrowButton("##up", ImGuiDir_Left)) {
      std::swap(sprite_.frames[index], sprite_.frames[index - 1]);
      if (active_frame_index_ == index)
        active_frame_index_ = index - 1;
      else if (active_frame_index_ == index - 1)
        active_frame_index_ = index;
    }
  }
  if (index < sprite_.frames.size() - 1) {
    gui_->SameLine();
    if (gui_->ArrowButton("##down", ImGuiDir_Right)) {
      std::swap(sprite_.frames[index], sprite_.frames[index + 1]);
      if (active_frame_index_ == index)
        active_frame_index_ = index + 1;
      else if (active_frame_index_ == index + 1)
        active_frame_index_ = index;
    }
  }

  gui_->Text("Frame %d", index);

  // Preview Image
  int tex_w = 0, tex_h = 0;
  if (sprite_.sdl_texture != nullptr) {
    SDL_QueryTexture(SdlTexture(), nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w < frame.texture_w) frame.texture_w = tex_w;
    if (tex_h < frame.texture_h) frame.texture_h = tex_h;

    // Calculate UVs
    ImVec2 uv0((float)frame.texture_x / tex_w, (float)frame.texture_y / tex_h);
    ImVec2 uv1((float)(frame.texture_x + frame.texture_w) / tex_w,
               (float)(frame.texture_y + frame.texture_h) / tex_h);

    // Fixed display size height, maintain aspect ratio
    float display_h = 100.0f;
    float aspect = (frame.texture_h > 0) ? (float)frame.texture_w / frame.texture_h : 1.0f;
    float display_w = display_h * aspect;

    gui_->Image(ImTextureId(), ImVec2(display_w, display_h), uv0, uv1, ImVec4(1, 1, 1, 1),
                ImVec4(1, 1, 1, 0.5f));
  } else {
    gui_->Button("No Texture", ImVec2(100, 100));
  }

  // Editable fields with validation
  gui_->PushItemWidth(80);

  // Helper lambda for integer fields to reduce duplication
  auto RenderIntField = [&](const char* label, int* value, int min = 0, int max = 10000) {
    gui_->AlignTextToFramePadding();
    gui_->Text("%s", label);
    gui_->SameLine();
    // Offset for alignment
    gui_->SetCursorPosX(start_x + 80.0f);
    // Use label for unique ID if needed, but here we just need unique ID for InputInt
    // Using ## + label might conflict if same label used twice (e.g. W/H), so we rely on
    // pushID(index) Actually we need unique IDs per field.
    if (gui_->InputInt(absl::StrCat("##", label).c_str(), value)) {
      if (*value < min) *value = min;
      if (*value > max) *value = max;
    }
  };

  // Note: We use unique labels/ids here
  RenderIntField("X:", &frame.texture_x, 0, tex_w > 0 ? tex_w - frame.texture_w : 0);
  RenderIntField("Y:", &frame.texture_y, 0, tex_h > 0 ? tex_h - frame.texture_h : 0);
  RenderIntField("W:", &frame.texture_w, 0, tex_w > 0 ? tex_w - frame.texture_x : 0);
  RenderIntField("H:", &frame.texture_h, 0, tex_h > 0 ? tex_h - frame.texture_y : 0);

  gui_->Text("Render:");
  RenderIntField("Render W:", &frame.render_w, 1, 10000);
  RenderIntField("Render H:", &frame.render_h, 1, 10000);

  gui_->Text("Offsets:");
  RenderIntField("Offset X:", &frame.offset_x, -10000, 10000);
  RenderIntField("Offset Y:", &frame.offset_y, -10000, 10000);

  gui_->Text("Anim:");
  RenderIntField("Duration:", &frame.frames_per_cycle, 1, 1000);

  // Scale Tool
  gui_->Separator();
  gui_->AlignTextToFramePadding();
  gui_->Text("Scale:");
  static int render_scale_input = 2;
  gui_->SameLine();
  gui_->SetCursorPosX(start_x + 80.0f);
  if (gui_->InputInt("##scale", &render_scale_input)) {
    if (render_scale_input < 1) render_scale_input = 1;
  }

  gui_->Indent(80.0f);
  if (gui_->Button("Apply Scale")) {
    frame.render_w = frame.texture_w * render_scale_input;
    frame.render_h = frame.texture_h * render_scale_input;
  }
  gui_->Unindent(80.0f);

  gui_->PopItemWidth();
}

void SpriteEditor::RenderFullTextureView() {
  if (sprite_.sdl_texture == nullptr) return;

  gui_->Separator();
  gui_->Text("Full Texture (Interact to Edit)");

  // Zoom controls
  if (gui_->Button("-")) full_texture_zoom_ = std::max(0.1f, full_texture_zoom_ - 0.1f);
  gui_->SameLine();
  if (gui_->Button("+")) full_texture_zoom_ = std::min(5.0f, full_texture_zoom_ + 0.1f);
  gui_->SameLine();
  gui_->Text("Zoom: %.1fx", full_texture_zoom_);

  int tex_w, tex_h;
  SDL_QueryTexture(SdlTexture(), nullptr, nullptr, &tex_w, &tex_h);

  ImVec2 canvas_size = ImVec2((float)tex_w * full_texture_zoom_, (float)tex_h * full_texture_zoom_);

  ScopedChild child(gui_, "FullTextureRegion", ImVec2(0, 400), true,
                    ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

  ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  gui_->Image(ImTextureId(), canvas_size);

  // Early return if no active frame allows us to skip interaction logic
  if (active_frame_index_ < 0 || active_frame_index_ >= (int)sprite_.frames.size()) {
    return;
  }

  SpriteFrame& active_frame = sprite_.frames[active_frame_index_];
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Draw current rect
  ImVec2 rect_min = ImVec2(canvas_pos.x + active_frame.texture_x * full_texture_zoom_,
                           canvas_pos.y + active_frame.texture_y * full_texture_zoom_);
  ImVec2 rect_max = ImVec2(rect_min.x + active_frame.texture_w * full_texture_zoom_,
                           rect_min.y + active_frame.texture_h * full_texture_zoom_);

  draw_list->AddRect(rect_min, rect_max, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);

  // Interaction
  ImGui::SetCursorScreenPos(canvas_pos);
  ImGui::InvisibleButton("TextureOverlay", canvas_size);

  // Early return if not interacting
  if (!ImGui::IsItemActive() || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    is_dragging_rect_ = false;
    return;
  }

  // Handle Dragging
  ImVec2 mouse_pos = ImGui::GetMousePos();
  float rel_x = (mouse_pos.x - canvas_pos.x) / full_texture_zoom_;
  float rel_y = (mouse_pos.y - canvas_pos.y) / full_texture_zoom_;

  // Clamp to texture bounds
  rel_x = std::max(0.0f, std::min(rel_x, (float)tex_w));
  rel_y = std::max(0.0f, std::min(rel_y, (float)tex_h));

  if (!is_dragging_rect_) {
    is_dragging_rect_ = true;
    drag_start_ = ImVec2(rel_x, rel_y);
    active_frame.texture_x = (int)rel_x;
    active_frame.texture_y = (int)rel_y;
    active_frame.texture_w = 0;
    active_frame.texture_h = 0;
  } else {
    // Update WH based on drag
    int start_x = (int)drag_start_.x;
    int start_y = (int)drag_start_.y;
    int curr_x = (int)rel_x;
    int curr_y = (int)rel_y;

    // Allow dragging in any direction
    active_frame.texture_x = std::min(start_x, curr_x);
    active_frame.texture_y = std::min(start_y, curr_y);
    active_frame.texture_w = std::abs(curr_x - start_x);
    active_frame.texture_h = std::abs(curr_y - start_y);
  }
}

}  // namespace zebes