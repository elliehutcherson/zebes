#include "editor/sprite_editor/sprite_editor.h"

#include <algorithm>

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
constexpr float kAnimationPreviewMaximumHeight = 300.0f;
constexpr float kAnimationPreviewPadding = 8.0f;

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
    model_.sprite().texture_handle = {};
    LOG(ERROR) << "Failed to load sprite texture: " << texture.status();
    return;
  }

  absl::Status status = model_.SelectTexture(texture_id, (*texture)->texture_handle);
  if (!status.ok()) LOG(ERROR) << "Failed to select sprite texture: " << status;
}

void SpriteEditor::RefreshSpriteList() {
  model_.SetSprites(api_->GetAllSprites());
  LOG(INFO) << "Loaded " << model_.sprites().size() << " sprites.";

  absl::StatusOr<std::vector<Texture>> textures = api_->GetAllTextures();
  if (!textures.ok()) {
    LOG(ERROR) << "Failed to load textures: " << textures.status();
    model_.SetTextures({});
  } else {
    model_.SetTextures(std::move(*textures));
    LOG(INFO) << "Loaded " << model_.textures().size() << " textures.";
  }
}

void SpriteEditor::SelectSprite(const std::string& sprite_id) {
  absl::Status status = model_.SelectSprite(sprite_id);
  if (!status.ok()) {
    LOG(ERROR) << "Selected sprite not found in list: " << sprite_id << ": " << status;
    return;
  }
  LoadSpriteTexture(model_.sprite().texture_id);

  animator_->Reset();
  is_playing_animation_ = false;
  animation_timer_ = 0.0;
}

// Below the methods for upserting, updating and deleting sprites are defined.
void SpriteEditor::CreateSprite() {
  absl::StatusOr<Sprite> request = model_.BuildCreateRequest();
  if (!request.ok()) {
    LOG(ERROR) << "Cannot create sprite: " << request.status();
    return;
  }

  absl::StatusOr<std::string> new_id = api_->CreateSprite(*request);
  if (!new_id.ok()) {
    LOG(ERROR) << "Failed to create sprite: " << new_id.status();
    return;
  }

  LOG(INFO) << "Created new sprite: " << *new_id;
  RefreshSpriteList();
  SelectSprite(*new_id);
}

void SpriteEditor::UpdateSprite() {
  absl::StatusOr<Sprite> request = model_.BuildUpdateRequest();
  if (!request.ok()) {
    LOG(ERROR) << "Cannot update sprite: " << request.status();
    return;
  }

  absl::Status status = api_->UpdateSprite(*request);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to update sprite: " << status;
    return;
  }

  LOG(INFO) << "Updated sprite config.";
  model_.FinishSave();
  RefreshSpriteList();  // Refresh list to show new name
}

void SpriteEditor::DeleteSprite(const std::string& sprite_id) {
  absl::Status status = api_->DeleteSprite(sprite_id);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to delete sprite: " << status;
    return;
  }

  LOG(INFO) << "Deleted sprite " << sprite_id;
  model_.ClearSelection();
  RefreshSpriteList();
}

void SpriteEditor::SaveSpriteFrames() { UpdateSprite(); }

void SpriteEditor::Render() {
  RenderSpriteSelection();
  RenderSpriteFrameList();
  RenderFullTextureView();
}

void SpriteEditor::RenderSpriteSelection() {
  // Use tables for list and inspector
  auto table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
  ScopedTable table = gui_->CreateScopedTable("SpriteListSplit", 2, table_flags);
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
    model_.BeginNewSprite();
    animator_->Reset();
    is_playing_animation_ = false;
    animation_timer_ = 0.0;
  }

  // Create list with fixed height
  ScopedChild child = gui_->CreateScopedChild("##Sprites", ImVec2(0, kSpriteListHeight), false);
  for (const auto& catalog_entry : model_.sprites()) {
    const Sprite& sprite = catalog_entry.second;
    std::string label = sprite.name_id();
    bool is_selected = (model_.sprite().id == sprite.id && !model_.is_new_sprite());

    if (gui_->Selectable(label.c_str(), is_selected)) {
      SelectSprite(sprite.id);
    }
    if (is_selected) {
      ImGui::SetItemDefaultFocus();
    }
  }
}

void SpriteEditor::RenderSpriteMeta() {
  if (!model_.has_selection()) {
    gui_->Text("Select a sprite to edit or Create New.");
    return;
  }

  // Title
  Sprite& sprite = model_.sprite();
  std::string title = model_.is_new_sprite() ? "NewSprite" : absl::StrCat("Sprite: ", sprite.id);
  gui_->Text("%s", title.c_str());
  gui_->Separator();

  // ID is auto-assigned
  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(true);
    if (model_.is_new_sprite()) {
      gui_->Text("ID: <Auto>");
    } else {
      // Read-only ID
      gui_->InputText("ID", const_cast<char*>(sprite.id.c_str()), sprite.id.size(),
                      ImGuiInputTextFlags_ReadOnly);
    }
  }

  // Texture Dropdown
  // Find current texture path from ID for display
  std::string current_tex_path = "Select Texture";
  if (!sprite.texture_id.empty()) {
    for (const auto& catalog_entry : model_.textures()) {
      const Texture& tex = catalog_entry.second;
      if (tex.id == sprite.texture_id) {
        current_tex_path = tex.path;
        break;
      }
    }
  }

  {
    ScopedDisabled disabled = gui_->CreateScopedDisabled(!model_.is_new_sprite());
    ScopedCombo combo = gui_->CreateScopedCombo("Texture", current_tex_path.c_str());
    for (const auto& catalog_entry : model_.textures()) {
      const Texture& texture = catalog_entry.second;
      bool is_selected = (sprite.texture_id == texture.id);

      if (gui_->Selectable(texture.path.c_str(), is_selected)) {
        LoadSpriteTexture(texture.id);
      }

      if (is_selected) ImGui::SetItemDefaultFocus();
    }
  }

  // Sprite Name
  std::string& edit_name = model_.edit_name_buffer();
  if (gui_->InputText("Name", edit_name.data(), edit_name.size())) {
    // Handled by buffer
  }

  gui_->Spacing();
  gui_->Separator();
  gui_->Spacing();

  // Allow the user to create, update or delete sprite
  if (model_.is_new_sprite() && gui_->Button("Create Sprite")) {
    CreateSprite();
  } else if (!model_.is_new_sprite() && gui_->Button("Save Sprite Config")) {
    UpdateSprite();
  }
  gui_->SameLine();

  {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (gui_->Button("Delete Sprite")) {
      DeleteSprite(sprite.id);
    }
  }
}

void SpriteEditor::RenderSpriteAnimation() {
  gui_->Text("Animation Preview");
  const std::vector<SpriteFrame>& frames = model_.sprite().frames;

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
      animator_->Update(frames);
      animation_timer_ -= tick_duration;
    }
  }

  // Handle empty frames gracefully
  if (frames.empty()) {
    gui_->TextDisabled("No frames to animate.");
    return;
  }

  // Render Animated Frame
  absl::StatusOr<SpriteFrame> frame = animator_->GetCurrentFrame(frames);
  absl::StatusOr<int> current_frame_index = animator_->GetCurrentFrameIndex(frames);
  if (!frame.ok()) {
    if (is_playing_animation_) {
      gui_->Text("Animation Error: %s", frame.status().ToString().c_str());
    } else {
      gui_->Text("Press Play to start animation.");
    }
    return;
  }
  if (!current_frame_index.ok()) return;

  // Calculate UVs
  int tex_w = 0, tex_h = 0;
  if (model_.sprite().texture_handle) {
    SDL_QueryTexture(SdlTexture(), nullptr, nullptr, &tex_w, &tex_h);
  }

  if (tex_w > 0 && tex_h > 0) {
    const float available_width =
        std::max(1.0f, gui_->GetContentRegionAvail().x - 2.0f * kAnimationPreviewPadding);
    absl::StatusOr<AnimationPreviewLayout> layout =
        SpriteEditorModel::CalculateAnimationPreviewLayout(
            frames, *current_frame_index, available_width, kAnimationPreviewMaximumHeight);
    if (!layout.ok()) {
      gui_->Text("Invalid animation layout: %s", layout.status().ToString().c_str());
      return;
    }

    ImVec2 uv0(static_cast<float>(frame->texture_x) / tex_w,
               static_cast<float>(frame->texture_y) / tex_h);
    ImVec2 uv1(static_cast<float>(frame->texture_x + frame->texture_w) / tex_w,
               static_cast<float>(frame->texture_y + frame->texture_h) / tex_h);

    const ImVec2 canvas_position = gui_->GetCursorScreenPos();
    const ImVec2 canvas_size(layout->canvas_width + 2.0f * kAnimationPreviewPadding,
                             layout->canvas_height + 2.0f * kAnimationPreviewPadding);
    gui_->InvisibleButton("##AnimationPreviewCanvas", canvas_size);

    ImDrawList* draw_list = gui_->GetWindowDrawList();
    if (draw_list != nullptr) {
      const ImVec2 canvas_end(canvas_position.x + canvas_size.x, canvas_position.y + canvas_size.y);
      draw_list->AddRectFilled(canvas_position, canvas_end, IM_COL32(28, 28, 32, 255));
      draw_list->AddRect(canvas_position, canvas_end, IM_COL32(80, 80, 88, 255));

      const float origin_x = canvas_position.x + kAnimationPreviewPadding +
                             static_cast<float>(-layout->bounds_left) * layout->scale;
      const float origin_y = canvas_position.y + kAnimationPreviewPadding +
                             static_cast<float>(-layout->bounds_top) * layout->scale;
      draw_list->AddLine(ImVec2(origin_x - 4.0f, origin_y), ImVec2(origin_x + 4.0f, origin_y),
                         IM_COL32(120, 120, 128, 180));
      draw_list->AddLine(ImVec2(origin_x, origin_y - 4.0f), ImVec2(origin_x, origin_y + 4.0f),
                         IM_COL32(120, 120, 128, 180));

      const ImVec2 image_min(canvas_position.x + kAnimationPreviewPadding + layout->frame_x,
                             canvas_position.y + kAnimationPreviewPadding + layout->frame_y);
      const ImVec2 image_max(image_min.x + layout->frame_width, image_min.y + layout->frame_height);
      draw_list->AddImage(ImTextureId(), image_min, image_max, uv0, uv1);
    }
  } else {
    gui_->Text("Invalid texture dimensions.");
  }

  gui_->Text("Frame Index: %d", *current_frame_index);  // Debug info
}

void SpriteEditor::RenderSpriteFrameList() {
  if (!model_.has_selection()) return;

  gui_->Separator();
  Sprite& sprite = model_.sprite();
  std::string header_text = model_.is_new_sprite()
                                ? "Sprite Frames (New Sprite)"
                                : absl::StrCat("Sprite Frames for ID: ", sprite.id);
  gui_->Text("%s", header_text.c_str());

  // Controls
  if (gui_->Button("Add Frame")) {
    model_.AddFrame().IgnoreError();
  }
  if (!model_.is_new_sprite()) {
    gui_->SameLine();
    if (gui_->Button("Save Changes")) {
      SaveSpriteFrames();
    }

    gui_->SameLine();
    if (gui_->Button("Reset Changes")) {
      model_.ResetFrames();
    }
  }

  // Horizontal scroll area
  float min_height = sprite.frames.empty() ? 0.0f : 550.0f;
  ScopedChild child = gui_->CreateScopedChild("SpriteFramesList", ImVec2(0, min_height));

  if (sprite.frames.empty()) {
    gui_->TextDisabled("No frames found.");
  } else {
    for (int i = 0; i < static_cast<int>(sprite.frames.size()); ++i) {
      RenderSpriteFrameItem(i, sprite.frames[i]);
      if (i >= static_cast<int>(sprite.frames.size()) - 1) break;

      gui_->SameLine();
      gui_->Dummy(ImVec2(10, 0));
      gui_->SameLine();
    }
  }
}

void SpriteEditor::RenderSpriteFrameItem(int index, SpriteFrame& frame) {
  float start_x = ImGui::GetCursorPosX();
  ScopedGroup group = gui_->CreateScopedGroup();
  ScopedId id = gui_->CreateScopedId(index);

  // Header / Active Toggle
  bool is_active = (model_.active_frame_index() == index);
  if (is_active) {
    ScopedStyleColor style =
        gui_->CreateScopedStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    if (gui_->Button(absl::StrCat("Active ##", index).c_str())) {
      model_.ToggleActiveFrame(index);
    }
  } else if (gui_->Button(absl::StrCat("Edit ##", index).c_str())) {
    model_.ToggleActiveFrame(index);
  }

  gui_->SameLine();
  if (gui_->Button("X")) {
    model_.DeleteFrame(index).IgnoreError();
    return;  // Stop rendering this item (ScopedId/Group destructors called)
  }

  // Ordering buttons
  if (index > 0) {
    gui_->SameLine();
    if (gui_->ArrowButton("##up", ImGuiDir_Left)) {
      model_.MoveFrameLeft(index).IgnoreError();
      return;
    }
  }
  if (index < static_cast<int>(model_.sprite().frames.size()) - 1) {
    gui_->SameLine();
    if (gui_->ArrowButton("##down", ImGuiDir_Right)) {
      model_.MoveFrameRight(index).IgnoreError();
      return;
    }
  }

  gui_->Text("Frame %d", index);

  // Preview Image
  int tex_w = 0, tex_h = 0;
  if (model_.sprite().texture_handle) {
    SDL_QueryTexture(SdlTexture(), nullptr, nullptr, &tex_w, &tex_h);
    model_.ClampFrameToTexture(index, tex_w, tex_h).IgnoreError();

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
    model_.ApplyFrameScale(index, render_scale_input).IgnoreError();
  }
  gui_->Unindent(80.0f);

  gui_->PopItemWidth();
}

void SpriteEditor::RenderFullTextureView() {
  if (!model_.sprite().texture_handle) return;

  gui_->Separator();
  gui_->Text("Full Texture (Interact to Edit)");

  // Zoom controls
  if (gui_->Button("-")) model_.ZoomTextureOut();
  gui_->SameLine();
  if (gui_->Button("+")) model_.ZoomTextureIn();
  gui_->SameLine();
  gui_->Text("Zoom: %.1fx", model_.texture_zoom());

  int tex_w, tex_h;
  SDL_QueryTexture(SdlTexture(), nullptr, nullptr, &tex_w, &tex_h);

  const float texture_zoom = model_.texture_zoom();
  ImVec2 canvas_size = ImVec2((float)tex_w * texture_zoom, (float)tex_h * texture_zoom);

  ScopedChild child =
      gui_->CreateScopedChild("FullTextureRegion", ImVec2(0, 400), true,
                              ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

  ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  gui_->Image(ImTextureId(), canvas_size);

  // Early return if no active frame allows us to skip interaction logic
  const int active_frame_index = model_.active_frame_index();
  if (active_frame_index < 0 ||
      active_frame_index >= static_cast<int>(model_.sprite().frames.size())) {
    return;
  }

  SpriteFrame& active_frame = model_.sprite().frames[active_frame_index];
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Draw current rect
  ImVec2 rect_min = ImVec2(canvas_pos.x + active_frame.texture_x * texture_zoom,
                           canvas_pos.y + active_frame.texture_y * texture_zoom);
  ImVec2 rect_max = ImVec2(rect_min.x + active_frame.texture_w * texture_zoom,
                           rect_min.y + active_frame.texture_h * texture_zoom);

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
  float rel_x = (mouse_pos.x - canvas_pos.x) / texture_zoom;
  float rel_y = (mouse_pos.y - canvas_pos.y) / texture_zoom;

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
