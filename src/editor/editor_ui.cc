#include "editor/editor_ui.h"

#include <filesystem>

#include "ImGuiFileDialog.h"
#include "SDL_image.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "editor/sdl_wrapper.h"
#include "imgui.h"
#include "objects/sprite_interface.h"
#include "objects/texture.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<EditorUi>> EditorUi::Create(SdlWrapper* sdl, Api* api) {
  if (sdl == nullptr) {
    return absl::InvalidArgumentError("SDL wrapper is null");
  }
  if (api == nullptr) {
    return absl::InvalidArgumentError("API is null");
  }
  return absl::WrapUnique(new EditorUi(sdl, api));
}

EditorUi::EditorUi(SdlWrapper* sdl, Api* api)
    : sdl_(sdl),
      api_(api),
      texture_path_buffer_(256, '\0'),
      sprite_path_buffer_(256, '\0'),
      selected_texture_id_(0),
      sprite_id_input_(0),
      sprite_type_input_(0),
      sprite_x_input_(0),
      sprite_y_input_(0),
      sprite_w_input_(0),
      sprite_h_input_(0),
      animator_(std::make_unique<Animator>()) {}

void EditorUi::Render() {
  // Set up fullscreen window
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

  ImGui::Begin("Zebes Editor", nullptr, window_flags);

  if (ImGui::BeginTabBar("MainTabs")) {
    if (ImGui::BeginTabItem("Texture Editor")) {
      RenderTextureImport();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sprite Editor")) {
      RenderSpriteCreation();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Config Editor")) {
      RenderConfigEditor();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

void EditorUi::RenderTextureImport() {
  // Tabs handle visibility
  ImGui::InputText("Texture Path", texture_path_buffer_.data(), texture_path_buffer_.size());
  ImGui::SameLine();
  if (ImGui::Button("Open")) {
    IGFD::FileDialogConfig config;
    config.path = ".";
    ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".png,.jpg,.jpeg",
                                            config);
  }

  if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
      texture_path_buffer_ = filePathName;
      // Ensure buffer is large enough and null-terminated for InputText
      if (texture_path_buffer_.size() < 256) {
        texture_path_buffer_.resize(256, '\0');
      } else {
        // If path is longer than 256, resize to fit + null
        texture_path_buffer_.resize(texture_path_buffer_.size() + 1, '\0');
      }
    }
    ImGuiFileDialog::Instance()->Close();
  }

  if (ImGui::Button("Import Texture")) {
    std::string path = texture_path_buffer_.c_str();
    LOG(INFO) << "Importing texture from: " << path;
    absl::StatusOr<std::string> result = api_->CreateTexture(path);
    if (!result.ok()) {
      LOG(ERROR) << "Failed to create texture: " << result.status();
    } else {
      LOG(INFO) << "Texture created: " << *result;
      // Refresh list after import
      RefreshTextures();
    }
  }

  ImGui::Separator();
  ImGui::Text("Imported Textures");

  // Refresh list if empty (or add a refresh button)
  if (texture_list_.empty() && ImGui::Button("Refresh List")) {
    RefreshTextures();
  }

  // Auto-refresh on first load
  if (texture_list_.empty()) {
    RefreshTextures();
  }

  // List textures
  if (ImGui::BeginListBox("##Textures",
                          ImVec2(-FLT_MIN, 5 * ImGui::GetTextLineHeightWithSpacing()))) {
    for (const Texture& texture : texture_list_) {
      bool is_selected = (selected_texture_id_ == texture.id);
      std::string label = absl::StrCat(texture.id, ": ", texture.path);
      if (ImGui::Selectable(label.c_str(), is_selected)) {
        selected_texture_id_ = texture.id;
        LoadTexturePreview(texture.path);
        // Also update the texture path buffer for sprite creation
        texture_path_buffer_ = texture.path;
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndListBox();
  }

  // Preview
  if (!preview_texture_) {
    return;
  }

  ImGui::Separator();
  ImGui::Text("Texture Preview");

  // Zoom controls
  if (ImGui::Button("-")) {
    texture_preview_zoom_ *= 0.8f;
    if (texture_preview_zoom_ < 0.1f) texture_preview_zoom_ = 0.1f;
  }
  ImGui::SameLine();
  if (ImGui::Button("+")) {
    texture_preview_zoom_ *= 1.25f;
    if (texture_preview_zoom_ > 10.0f) texture_preview_zoom_ = 10.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Zoom")) {
    texture_preview_zoom_ = 1.0f;
  }
  ImGui::SameLine();
  ImGui::Text("Zoom: %.1fx", texture_preview_zoom_);

  int w = 0, h = 0;
  SDL_QueryTexture(preview_texture_, nullptr, nullptr, &w, &h);
  float aspect = (float)w / (float)h;
  float preview_w = 200.0f * texture_preview_zoom_;
  float preview_h = preview_w / aspect;

  ImGui::Text("Size: %dx%d", w, h);

  // Display the image with zoom applied
  ImGui::BeginChild("PreviewRegion", ImVec2(0, 400), true, ImGuiWindowFlags_HorizontalScrollbar);

  // Mouse wheel zoom when hovering over preview
  if (ImGui::IsWindowHovered()) {
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
      texture_preview_zoom_ *= (1.0f + wheel * 0.1f);
      if (texture_preview_zoom_ < 0.1f) texture_preview_zoom_ = 0.1f;
      if (texture_preview_zoom_ > 10.0f) texture_preview_zoom_ = 10.0f;
    }
  }

  ImGui::Image(reinterpret_cast<ImTextureID>(preview_texture_), ImVec2(preview_w, preview_h));
  ImGui::EndChild();
}

void EditorUi::RenderSpriteCreation() {
  // Tabs handle visibility
  RenderSpriteList();
  RenderSpriteFrameList();
  RenderFullTextureView();
}

void EditorUi::RenderSpriteList() {
  // Sprite List Section
  ImGui::Text("Existing Sprites");

  // Use columns for list and inspector
  ImGui::Columns(2, "SpriteListSplit", false);

  // Auto-refresh once
  static bool initial_sprite_load = false;
  if (!initial_sprite_load) {
    RefreshSpriteList();
    initial_sprite_load = true;
  }

  if (ImGui::Button("Refresh Sprite List")) {
    RefreshSpriteList();
  }
  ImGui::SameLine();
  if (ImGui::Button("Create New Sprite")) {
    is_creating_new_sprite_ = true;
    selected_sprite_id_ = 0;
    // Clear/Init buffers
    selected_sprite_config_ = SpriteConfig();
    edit_type_name_buffer_ = "";
    edit_type_name_buffer_.resize(256, '\0');
    current_sprite_frames_.clear();
    original_sprite_frames_.clear();
    active_sprite_frame_index_ = -1;
  }

  // Define a consistent height for both panels using 1-frame lag layout
  static float section_height = 300.0f;  // Default start height

  // List sprites
  // Minus 20 because the list would otherwise be longer than the sprite inspector
  // Using BeginChild with border=false to remove the "box" look
  ImGui::BeginChild("##Sprites", ImVec2(0, section_height - 20), false);
  for (const SpriteConfig& sprite : sprite_list_) {
    std::string label = absl::StrCat(sprite.id, ": ", sprite.type_name);
    bool is_selected = (selected_sprite_id_ == sprite.id && !is_creating_new_sprite_);

    if (ImGui::Selectable(label.c_str(), is_selected)) {
      SelectSprite(sprite.id);
    }
    if (is_selected) {
      ImGui::SetItemDefaultFocus();
    }
  }
  ImGui::EndChild();

  ImGui::NextColumn();

  // Draw Inspector and measure its height
  float start_y = ImGui::GetCursorPosY();
  RenderSpriteInspector();
  float end_y = ImGui::GetCursorPosY();

  // Update height for next frame (clamped to a minimum)
  float content_height = end_y - start_y;
  section_height = std::max(content_height, 200.0f);

  ImGui::Columns(1);
}

void EditorUi::RenderSpriteFrameList() {
  if (selected_sprite_id_ == 0) return;

  ImGui::Separator();
  ImGui::Text("Sprite Frames for ID: %d", selected_sprite_id_);

  // Controls
  if (ImGui::Button("Add Frame")) {
    SpriteFrame new_frame;
    new_frame.texture_w = 32;  // Default size
    new_frame.texture_h = 32;
    new_frame.render_w = 32;
    new_frame.render_h = 32;
    current_sprite_frames_.push_back(new_frame);
    active_sprite_frame_index_ = static_cast<int>(current_sprite_frames_.size()) - 1;
  }
  ImGui::SameLine();

  if (ImGui::Button("Save Changes")) {
    SaveSpriteFrames();
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset Changes")) {
    current_sprite_frames_ = original_sprite_frames_;
    active_sprite_frame_index_ = -1;
  }

  if (current_sprite_frames_.empty()) {
    ImGui::TextDisabled("No frames found.");
    return;
  }

  // Horizontal scroll area
  ImGui::BeginChild("SpriteFramesList", ImVec2(0, 360), true, ImGuiWindowFlags_HorizontalScrollbar);

  for (int i = 0; i < current_sprite_frames_.size(); ++i) {
    RenderSpriteFrameItem(i, current_sprite_frames_[i]);
  }
  ImGui::EndChild();
}

void EditorUi::RenderSpriteFrameItem(size_t index, SpriteFrame& frame) {
  float start_x = ImGui::GetCursorPosX();
  ImGui::BeginGroup();

  ImGui::PushID(static_cast<int>(index));

  // Header / Active Toggle
  bool is_active = (active_sprite_frame_index_ == (int)index);
  if (is_active) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    if (ImGui::Button(absl::StrCat("Active ##", index).c_str())) {
      active_sprite_frame_index_ = -1;  // Deselect
    }
    ImGui::PopStyleColor();
  } else if (ImGui::Button(absl::StrCat("Edit ##", index).c_str())) {
    active_sprite_frame_index_ = (int)index;
  }

  ImGui::SameLine();
  if (ImGui::Button("X")) {
    // Delete this frame
    if (active_sprite_frame_index_ == (int)index)
      active_sprite_frame_index_ = -1;
    else if (active_sprite_frame_index_ > (int)index)
      active_sprite_frame_index_--;

    current_sprite_frames_.erase(current_sprite_frames_.begin() + index);
    ImGui::PopID();
    ImGui::EndGroup();
    return;  // Stop rendering this item
  }

  // Ordering buttons
  if (index > 0) {
    ImGui::SameLine();
    if (ImGui::ArrowButton("##up", ImGuiDir_Left)) {
      std::swap(current_sprite_frames_[index], current_sprite_frames_[index - 1]);
      if (active_sprite_frame_index_ == index)
        active_sprite_frame_index_ = index - 1;
      else if (active_sprite_frame_index_ == index - 1)
        active_sprite_frame_index_ = index;
    }
  }
  if (index < current_sprite_frames_.size() - 1) {
    ImGui::SameLine();
    if (ImGui::ArrowButton("##down", ImGuiDir_Right)) {
      std::swap(current_sprite_frames_[index], current_sprite_frames_[index + 1]);
      if (active_sprite_frame_index_ == index)
        active_sprite_frame_index_ = index + 1;
      else if (active_sprite_frame_index_ == index + 1)
        active_sprite_frame_index_ = index;
    }
  }

  ImGui::Text("Frame %zu (ID: %d)", index, frame.id);

  // Preview Image
  int tex_w = 0, tex_h = 0;
  if (sprite_texture_) {
    SDL_QueryTexture(sprite_texture_, nullptr, nullptr, &tex_w, &tex_h);

    // Calculate UVs
    ImVec2 uv0((float)frame.texture_x / tex_w, (float)frame.texture_y / tex_h);
    ImVec2 uv1((float)(frame.texture_x + frame.texture_w) / tex_w,
               (float)(frame.texture_y + frame.texture_h) / tex_h);

    // Fixed display size height, maintain aspect ratio
    float display_h = 100.0f;
    float aspect = (frame.texture_h > 0) ? (float)frame.texture_w / frame.texture_h : 1.0f;
    float display_w = display_h * aspect;

    ImGui::Image((ImTextureID)sprite_texture_, ImVec2(display_w, display_h), uv0, uv1,
                 ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
  } else {
    ImGui::Button("No Texture", ImVec2(100, 100));
  }

  // Editable fields with validation
  ImGui::PushItemWidth(80);

  auto clamp = [](int& val, int min, int max) {
    if (val < min) val = min;
    if (val > max) val = max;
  };

  // Align inputs to the longest label "Duration:"
  const float label_offset = 80.0f;

  ImGui::AlignTextToFramePadding();
  ImGui::Text("X:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  if (ImGui::InputInt("##tx", &frame.texture_x)) {
    clamp(frame.texture_x, 0, tex_w > 0 ? tex_w - frame.texture_w : 0);
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Y:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  if (ImGui::InputInt("##ty", &frame.texture_y)) {
    clamp(frame.texture_y, 0, tex_h > 0 ? tex_h - frame.texture_h : 0);
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("W:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  if (ImGui::InputInt("##tw", &frame.texture_w)) {
    clamp(frame.texture_w, 0, tex_w > 0 ? tex_w - frame.texture_x : 0);
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("H:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  if (ImGui::InputInt("##th", &frame.texture_h)) {
    clamp(frame.texture_h, 0, tex_h > 0 ? tex_h - frame.texture_y : 0);
  }

  // Render Offsets
  ImGui::Text("Render:");
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Offset X:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  ImGui::InputInt("##rox", &frame.render_offset_x);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Offset Y:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  ImGui::InputInt("##roy", &frame.render_offset_y);

  // Render Size
  ImGui::AlignTextToFramePadding();
  ImGui::Text("H:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  ImGui::InputInt("##rh", &frame.render_h);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("W:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  ImGui::InputInt("##rw", &frame.render_w);

  // Frames per cycle
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Duration:");
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  if (ImGui::InputInt("##fpc", &frame.frames_per_cycle)) {
    if (frame.frames_per_cycle < 1) frame.frames_per_cycle = 1;
  }

  // Scale Tool
  ImGui::Separator();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Scale:");
  static int render_scale_input = 2;
  ImGui::SameLine();
  ImGui::SetCursorPosX(start_x + label_offset);
  if (ImGui::InputInt("##scale", &render_scale_input)) {
    if (render_scale_input < 1) render_scale_input = 1;
  }

  ImGui::Indent(label_offset);
  if (ImGui::Button("Apply Scale")) {
    frame.render_w = frame.texture_w * render_scale_input;
    frame.render_h = frame.texture_h * render_scale_input;
  }
  ImGui::Unindent(label_offset);

  ImGui::PopItemWidth();
  ImGui::PopID();
  ImGui::EndGroup();

  ImGui::SameLine();
  ImGui::Dummy(ImVec2(10, 0));
  ImGui::SameLine();
}

void EditorUi::RenderFullTextureView() {
  if (!sprite_texture_) return;

  ImGui::Separator();
  ImGui::Text("Full Texture (Interact to Edit)");

  // Zoom controls
  if (ImGui::Button("-")) full_texture_zoom_ = std::max(0.1f, full_texture_zoom_ - 0.1f);
  ImGui::SameLine();
  if (ImGui::Button("+")) full_texture_zoom_ = std::min(5.0f, full_texture_zoom_ + 0.1f);
  ImGui::SameLine();
  ImGui::Text("Zoom: %.1fx", full_texture_zoom_);

  int tex_w, tex_h;
  SDL_QueryTexture(sprite_texture_, nullptr, nullptr, &tex_w, &tex_h);

  ImVec2 canvas_size = ImVec2((float)tex_w * full_texture_zoom_, (float)tex_h * full_texture_zoom_);

  ImGui::BeginChild("FullTextureRegion", ImVec2(0, 400), true,
                    ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

  ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImGui::Image((ImTextureID)sprite_texture_, canvas_size);

  // Overlay / Interaction
  if (active_sprite_frame_index_ >= 0 &&
      active_sprite_frame_index_ < (int)current_sprite_frames_.size()) {
    SpriteFrame& active_frame = current_sprite_frames_[active_sprite_frame_index_];
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
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
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
    } else {
      is_dragging_rect_ = false;
    }
  }

  ImGui::EndChild();
}

void EditorUi::LoadSpriteTexture(int sprite_id) {
  // Load texture
  if (sprite_texture_) {
    if (sdl_wrapper_) sdl_->DestroyTexture(sprite_texture_);
    sprite_texture_ = nullptr;
  }

  // Find the sprite config to get the path
  std::string path;
  for (const SpriteConfig& s : sprite_list_) {
    if (s.id == sprite_id) {
      path = s.texture_path;
      break;
    }
  }

  if (path.empty()) {
    return;
  }

  if (!std::filesystem::exists(path)) {
    LOG(ERROR) << "Texture path does not exist: " << path;
    return;
  }

  absl::StatusOr<SDL_Texture*> texture_or_err = sdl_->CreateTexture(path);
  if (!texture_or_err.ok()) {
    LOG(ERROR) << "Failed to load sprite texture: " << texture_or_err.status();
    return;
  }
  sprite_texture_ = *texture_or_err;
}

void EditorUi::LoadTexturePreview(const std::string& path) {
  if (preview_texture_ != nullptr) {
    sdl_->DestroyTexture(preview_texture_);
    preview_texture_ = nullptr;
  }

  // Check if file exists
  if (!std::filesystem::exists(path)) {
    LOG(ERROR) << "Texture file not found: " << path;
    return;
  }

  absl::StatusOr<SDL_Texture*> texture = sdl_->CreateTexture(path);
  if (!texture.ok()) {
    LOG(ERROR) << "Failed to load texture preview: " << texture.status();
    return;
  }
  preview_texture_ = *texture;
}

// Helper buffer for strings
struct InputBuffer {
  std::string buffer;
  void Resize(size_t size) { buffer.resize(size, '\0'); }
  char* Data() { return buffer.data(); }
  size_t Size() { return buffer.size(); }
  void Set(const std::string& str) {
    buffer = str;
    buffer.reserve(256);
  }
};

void EditorUi::RenderConfigEditor() {
  if (!config_loaded_) {
    const GameConfig* current = api_->GetConfig();
    if (current != nullptr) {
      local_config_ = *current;
      window_title_buffer_ = local_config_.window.title;
      // Ensure buffer has extra capacity for editing
      if (window_title_buffer_.capacity() < 256) {
        window_title_buffer_.resize(256, '\0');
      }
      config_loaded_ = true;
    } else {
      ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load configuration.");
      return;
    }
  }

  if (ImGui::Button("Save Config")) {
    // Update title from buffer before saving
    local_config_.window.title = window_title_buffer_.c_str();  // Truncates at null terminator
    absl::Status status = api_->SaveConfig(local_config_);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to save config: " << status;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reload from Disk")) {
    config_loaded_ = false;  // Force reload on next frame
  }

  ImGui::Separator();
  ImGui::BeginChild("ConfigScrollRegion", ImVec2(0, 0), true);

  if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::InputInt("Target FPS", &local_config_.fps);
    ImGui::InputInt("Frame Delay (ms)", &local_config_.frame_delay);
    ImGui::InputFloat("Gravity", &local_config_.gravity);

    // Mode Enum
    const char* items[] = {"Player Mode", "Creator Mode"};
    int current_item = static_cast<int>(local_config_.mode);
    if (ImGui::Combo("Mode", &current_item, items, IM_ARRAYSIZE(items))) {
      local_config_.mode = static_cast<GameConfig::Mode>(current_item);
    }
  }

  if (ImGui::CollapsingHeader("Window Settings")) {
    ImGui::InputText("Title", window_title_buffer_.data(), window_title_buffer_.size());
    ImGui::InputInt("Width", &local_config_.window.width);
    ImGui::InputInt("Height", &local_config_.window.height);

    // Flags
    bool fullscreen = (local_config_.window.flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
      if (fullscreen)
        local_config_.window.flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
      else
        local_config_.window.flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;

      if (sdl_) {
        absl::Status s = sdl_->SetWindowFullscreen(fullscreen);
        if (!s.ok()) LOG(ERROR) << "Failed to set fullscreen: " << s;
      }
    }

    bool resizable = (local_config_.window.flags & SDL_WINDOW_RESIZABLE);
    if (ImGui::Checkbox("Resizable", &resizable)) {
      if (resizable)
        local_config_.window.flags |= SDL_WINDOW_RESIZABLE;
      else
        local_config_.window.flags &= ~SDL_WINDOW_RESIZABLE;

      if (sdl_) {
        absl::Status s = sdl_->SetWindowResizable(resizable);
        if (!s.ok()) LOG(ERROR) << "Failed to set resizable: " << s;
      }
    }

    bool high_dpi = (local_config_.window.flags & SDL_WINDOW_ALLOW_HIGHDPI);
    if (ImGui::Checkbox("High DPI", &high_dpi)) {
      if (high_dpi)
        local_config_.window.flags |= SDL_WINDOW_ALLOW_HIGHDPI;
      else
        local_config_.window.flags &= ~SDL_WINDOW_ALLOW_HIGHDPI;
    }
  }

  if (ImGui::CollapsingHeader("Boundaries")) {
    ImGui::InputInt("Min X", &local_config_.boundaries.x_min);
    ImGui::InputInt("Max X", &local_config_.boundaries.x_max);
    ImGui::InputInt("Min Y", &local_config_.boundaries.y_min);
    ImGui::InputInt("Max Y", &local_config_.boundaries.y_max);
  }

  if (ImGui::CollapsingHeader("Tiles")) {
    ImGui::InputInt("Scale", &local_config_.tiles.scale);
    ImGui::InputInt("Source Width", &local_config_.tiles.source_width);
    ImGui::InputInt("Source Height", &local_config_.tiles.source_height);
    ImGui::InputInt("Size X", &local_config_.tiles.size_x);
    ImGui::InputInt("Size Y", &local_config_.tiles.size_y);
  }

  if (ImGui::CollapsingHeader("Collision")) {
    ImGui::InputFloat("Area Width", &local_config_.collisions.area_width);
    ImGui::InputFloat("Area Height", &local_config_.collisions.area_height);
  }

  if (ImGui::CollapsingHeader("Player Defaults")) {
    ImGui::InputFloat("Start X", &local_config_.player_starting_x);
    ImGui::InputFloat("Start Y", &local_config_.player_starting_y);
    ImGui::InputFloat("Hitbox W", &local_config_.player_hitbox_w);
    ImGui::InputFloat("Hitbox H", &local_config_.player_hitbox_h);
  }

  if (ImGui::CollapsingHeader("Debug Flags")) {
    ImGui::Checkbox("Render Player Hitbox", &local_config_.enable_player_hitbox_render);
    ImGui::Checkbox("Render Tile Hitbox", &local_config_.enable_tile_hitbox_render);
    ImGui::Checkbox("Render HUD", &local_config_.enable_hud_render);
  }

  ImGui::EndChild();
}

void EditorUi::RefreshTextures() {
  auto textures = api_->GetAllTextures();
  if (textures.ok()) {
    texture_list_ = *textures;
    // Optional: Log success if needed, but usually redundant for every refresh
  } else {
    LOG(ERROR) << "Failed to refresh textures: " << textures.status();
  }
}

void EditorUi::RefreshSpriteList() {
  absl::StatusOr<std::vector<SpriteConfig>> sprites = api_->GetAllSprites();
  if (sprites.ok()) {
    sprite_list_ = *sprites;
    LOG(INFO) << "Loaded " << sprite_list_.size() << " sprites.";
  } else {
    LOG(ERROR) << "Failed to get sprites: " << sprites.status();
  }
}

void EditorUi::SelectSprite(int sprite_id) {
  is_creating_new_sprite_ = false;
  selected_sprite_id_ = sprite_id;
  // Find config and setup editing buffer
  for (const SpriteConfig& s : sprite_list_) {
    if (s.id == sprite_id) {
      selected_sprite_config_ = s;
      edit_type_name_buffer_ = s.type_name;
      // Ensure buffer has enough space for editing
      if (edit_type_name_buffer_.size() < 256) {
        edit_type_name_buffer_.resize(256, '\0');
      }
      break;
    }
  }

  absl::StatusOr<std::vector<SpriteFrame>> frames = api_->GetSpriteFrames(sprite_id);
  if (frames.ok()) {
    current_sprite_frames_ = *frames;
    original_sprite_frames_ = *frames;  // Store original state
  } else {
    LOG(ERROR) << "Failed to get sprite frames: " << frames.status();
    current_sprite_frames_.clear();
    original_sprite_frames_.clear();
  }
  LoadSpriteTexture(sprite_id);
  active_sprite_frame_index_ = -1;  // Reset active selection

  // Update config with loaded frames so animator has them
  selected_sprite_config_.sprite_frames = current_sprite_frames_;

  // Reset animator
  animator_->SetSpriteConfig(selected_sprite_config_);
  is_playing_animation_ = false;
  animation_timer_ = 0.0;
}

void EditorUi::RenderSpriteInspector() {
  if (selected_sprite_id_ == 0 && !is_creating_new_sprite_) {
    ImGui::Text("Select a sprite to edit or Create New.");
    return;
  }

  ImGui::Text(is_creating_new_sprite_ ? "Inspector: New Sprite" : "Inspector: Sprite %d",
              is_creating_new_sprite_ ? 0 : selected_sprite_id_);
  ImGui::Separator();

  ImGui::BeginDisabled(true);
  int id = selected_sprite_config_.id;
  if (is_creating_new_sprite_) {
    // ID is auto-assigned
    ImGui::Text("ID: <Auto>");
  } else {
    ImGui::InputInt("ID", &id);
  }
  ImGui::EndDisabled();

  // Texture Dropdown
  bool disable_texture = !is_creating_new_sprite_;
  if (!is_creating_new_sprite_) ImGui::BeginDisabled();

  if (ImGui::BeginCombo("Texture", selected_sprite_config_.texture_path.c_str())) {
    for (const Texture& texture : texture_list_) {
      bool is_selected = (selected_sprite_config_.texture_path == texture.path);

      // No need to LoadSpriteTexture here.
      if (ImGui::Selectable(texture.path.c_str(), is_selected)) {
        selected_sprite_config_.texture_path = texture.path;
      }

      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  // 3. Pop the disabled state
  if (!is_creating_new_sprite_) ImGui::EndDisabled();

  // Editable
  int type = selected_sprite_config_.type;
  if (ImGui::InputInt("Type", &type)) {
    selected_sprite_config_.type = static_cast<uint16_t>(type);
  }

  if (ImGui::InputText("Type Name", edit_type_name_buffer_.data(), edit_type_name_buffer_.size())) {
    // Handled by buffer
  }

  // Below the methods for upserting, updating and deleting sprites are defined.
  auto upsert_sprite = [&]() {
    selected_sprite_config_.type_name = edit_type_name_buffer_.c_str();
    if (selected_sprite_config_.texture_path.empty()) {
      LOG(ERROR) << "Texture path cannot be empty.";
      return;
    }

    absl::StatusOr<uint16_t> sprite_id = api_->UpsertSprite(selected_sprite_config_);
    if (!sprite_id.ok()) {
      LOG(ERROR) << "Failed to create sprite: " << sprite_id.status();
      return;
    }

    LOG(INFO) << "Created new sprite: " << *sprite_id;
    is_creating_new_sprite_ = false;
    RefreshSpriteList();
    SelectSprite(*sprite_id);
  };

  auto update_sprite = [&]() {
    // Update config from buffer
    selected_sprite_config_.type_name = edit_type_name_buffer_.c_str();

    absl::Status status = api_->UpdateSprite(selected_sprite_config_);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to update sprite: " << status;
      return;
    }

    LOG(INFO) << "Updated sprite config.";
    RefreshSpriteList();  // Refresh list to show new name
  };

  auto delete_sprite = [&]() {
    absl::Status status = api_->DeleteSprite(selected_sprite_id_);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to delete sprite: " << status;
      return;
    }

    LOG(INFO) << "Deleted sprite " << selected_sprite_id_;
    selected_sprite_id_ = 0;
    RefreshSpriteList();
    current_sprite_frames_.clear();
    original_sprite_frames_.clear();
    active_sprite_frame_index_ = -1;
  };

  if (is_creating_new_sprite_ && ImGui::Button("Create Sprite")) {
    upsert_sprite();
  } else if (ImGui::Button("Save Sprite Config")) {
    update_sprite();
  }

  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
  if (ImGui::Button("Delete Sprite")) {
    delete_sprite();
  }
  ImGui::PopStyleColor();

  // Custom separator that respects column width
  {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y),
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));  // Spacing
  }
  ImGui::Text("Animation Preview");

  // Play/Pause Control
  if (ImGui::Button(is_playing_animation_ ? "Pause" : "Play")) {
    is_playing_animation_ = !is_playing_animation_;
  }

  // Animation Logic
  if (is_playing_animation_) {
    // Assuming target FPS from config or default 60
    int target_fps = local_config_.fps > 0 ? local_config_.fps : 60;
    double tick_duration = 1.0 / target_fps;

    animation_timer_ += ImGui::GetIO().DeltaTime;
    while (animation_timer_ >= tick_duration) {
      animator_->Update();
      animation_timer_ -= tick_duration;
    }
  }

  // Render Animated Frame
  absl::StatusOr<SpriteFrame> frame = animator_->GetCurrentFrame();
  if (!frame.ok()) {
    if (is_playing_animation_) {
      ImGui::Text("Animation Error: %s", frame.status().ToString().c_str());
    } else {
      ImGui::Text("Press Play to start animation.");
    }
    return;
  }

  // Calculate UVs
  int tex_w = 0, tex_h = 0;
  if (sprite_texture_) {
    SDL_QueryTexture(sprite_texture_, nullptr, nullptr, &tex_w, &tex_h);
  }

  if (tex_w > 0 && tex_h > 0) {
    ImVec2 uv0(static_cast<float>(frame->texture_x) / tex_w,
               static_cast<float>(frame->texture_y) / tex_h);
    ImVec2 uv1(static_cast<float>(frame->texture_x + frame->texture_w) / tex_w,
               static_cast<float>(frame->texture_y + frame->texture_h) / tex_h);

    ImGui::Image((ImTextureID)sprite_texture_, ImVec2(frame->render_w, frame->render_h), uv0, uv1);
  } else {
    ImGui::Text("Invalid texture dimensions.");
  }

  ImGui::Text("Frame Index: %d", frame->index);  // Debug info
}

void EditorUi::SaveSpriteFrames() {
  // 1. Identify deleted frames
  for (const SpriteFrame& original : original_sprite_frames_) {
    bool found = false;
    for (const SpriteFrame& current : current_sprite_frames_) {
      if (found = (current.id == original.id); found) {
        break;
      }
    }
    if (found) continue;

    LOG(INFO) << "Deleting SpriteFrame ID: " << original.id;
    absl::Status status = api_->DeleteSpriteFrame(original.id);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to delete SpriteFrame " << original.id << ": " << status;
    }
  }

  // 2. Insert or Update current frames
  for (int i = 0; i < current_sprite_frames_.size(); ++i) {
    SpriteFrame& frame = current_sprite_frames_[i];
    frame.index = i;  // Update index based on current order

    if (frame.id == 0) {
      // New frame
      LOG(INFO) << "Inserting new SpriteFrame at index " << i;
      absl::StatusOr<uint16_t> frame_id = api_->InsertSpriteFrame(selected_sprite_id_, frame);
      if (frame_id.ok()) {
        frame.id = *frame_id;
      } else {
        LOG(ERROR) << "Failed to insert SpriteFrame: " << frame_id.status();
      }

      continue;
    }

    // Update existing
    // LOG(INFO) << "Updating SpriteFrame ID: " << frame.id << " at index " << i;
    absl::Status status = api_->UpdateSpriteFrame(frame);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to update SpriteFrame: " << status;
    }
  }

  LOG(INFO) << "Saved changes for sprite " << selected_sprite_id_;
  // Refresh original state
  original_sprite_frames_ = current_sprite_frames_;

  // Update config and animator
  selected_sprite_config_.sprite_frames = current_sprite_frames_;
  animator_->SetSpriteConfig(selected_sprite_config_);
}

}  // namespace zebes
