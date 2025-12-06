#include "editor/editor_ui.h"

#include <filesystem>

#include "ImGuiFileDialog.h"
#include "SDL_image.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "imgui.h"
#include "objects/sprite_interface.h"

namespace zebes {

EditorUI::EditorUI()
    : texture_path_buffer_(256, '\0'),
      sprite_path_buffer_(256, '\0'),
      selected_texture_id_(0),
      sprite_id_input_(0),
      sprite_type_input_(0),
      sprite_x_input_(0),
      sprite_y_input_(0),
      sprite_w_input_(0),
      sprite_h_input_(0) {}

void EditorUI::Render(Api* api) {
  // Set up fullscreen window
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

  ImGui::Begin("Zebes Editor", nullptr, window_flags);

  RenderTextureImport(api);
  RenderSpriteCreation(api);

  ImGui::End();
}

void EditorUI::RenderTextureImport(Api* api) {
  if (!ImGui::CollapsingHeader("Texture Import")) {
    return;
  }

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
    auto result = api->CreateTexture(path);
    if (!result.ok()) {
      LOG(ERROR) << "Failed to create texture: " << result.status();
    } else {
      LOG(INFO) << "Texture created: " << *result;
      // Refresh list after import
      auto textures = api->GetAllTextures();
      if (textures.ok()) {
        texture_list_ = *textures;
      }
    }
  }

  ImGui::Separator();
  ImGui::Text("Imported Textures");

  // Refresh list if empty (or add a refresh button)
  if (texture_list_.empty() && ImGui::Button("Refresh List")) {
    auto textures = api->GetAllTextures();
    if (textures.ok()) {
      texture_list_ = *textures;
    } else {
      LOG(ERROR) << "Failed to get textures: " << textures.status();
    }
  }

  // Auto-refresh on first load
  if (texture_list_.empty()) {
    auto textures = api->GetAllTextures();
    if (textures.ok()) {
      texture_list_ = *textures;
    }
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
  if (preview_texture_) {
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

    int w, h;
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
}

void EditorUI::RenderSpriteCreation(Api* api) {
  if (!ImGui::CollapsingHeader("Sprite Creation")) {
    return;
  }

  RenderSpriteList(api);
  RenderSpriteFrameList(api);
  RenderFullTextureView();

  ImGui::Separator();
  ImGui::Text("Create New Sprite");

  ImGui::InputInt("Sprite ID", &sprite_id_input_);
  ImGui::InputInt("Type", &sprite_type_input_);
  ImGui::InputText("Texture Path", texture_path_buffer_.data(), texture_path_buffer_.size());

  // SpriteFrame inputs
  ImGui::Text("Sprite Frame 0");
  ImGui::InputInt("Tex X", &sprite_x_input_);
  ImGui::InputInt("Tex Y", &sprite_y_input_);
  ImGui::InputInt("Tex W", &sprite_w_input_);
  ImGui::InputInt("Tex H", &sprite_h_input_);

  if (ImGui::Button("Create Sprite")) {
    SpriteConfig config;
    config.id = sprite_id_input_;
    config.type = sprite_type_input_;
    config.texture_path = texture_path_buffer_.c_str();

    SpriteFrame frame;
    frame.texture_x = sprite_x_input_;
    frame.texture_y = sprite_y_input_;
    frame.texture_w = sprite_w_input_;
    frame.texture_h = sprite_h_input_;
    // Set render dimensions to match texture dimensions by default
    frame.render_w = sprite_w_input_;
    frame.render_h = sprite_h_input_;

    config.sprite_frames.push_back(frame);

    LOG(INFO) << "Creating sprite with ID: " << config.id;
    auto result = api->UpsertSprite(config);
    if (!result.ok()) {
      LOG(ERROR) << "Failed to upsert sprite: " << result.status();
    } else {
      LOG(INFO) << "Sprite upserted, ID: " << *result;
      // Force refresh logic if needed, or rely on next frame
    }
  }
}

void EditorUI::RenderSpriteList(Api* api) {
  // Sprite List Section
  ImGui::Text("Existing Sprites");

  auto refresh_sprites = [&]() {
    absl::StatusOr<std::vector<SpriteConfig>> sprites = api->GetAllSprites();
    if (sprites.ok()) {
      sprite_list_ = *sprites;
      LOG(INFO) << "Loaded " << sprite_list_.size() << " sprites.";
    } else {
      LOG(ERROR) << "Failed to get sprites: " << sprites.status();
    }
  };

  // Auto-refresh once
  static bool initial_sprite_load = false;
  if (!initial_sprite_load) {
    refresh_sprites();
    initial_sprite_load = true;
  }

  if (ImGui::Button("Refresh Sprite List")) {
    refresh_sprites();
  }

  auto select_sprite = [&](int sprite_id) {
    selected_sprite_id_ = sprite_id;
    auto frames = api->GetSpriteFrames(sprite_id);
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
  };

  // List sprites
  if (ImGui::BeginListBox("##Sprites",
                          ImVec2(-FLT_MIN, 5 * ImGui::GetTextLineHeightWithSpacing()))) {
    for (const SpriteConfig& sprite : sprite_list_) {
      std::string label = absl::StrCat(sprite.id, ": ", sprite.type_name);
      bool is_selected = (selected_sprite_id_ == sprite.id);

      if (ImGui::Selectable(label.c_str(), is_selected)) {
        select_sprite(sprite.id);
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndListBox();
  }
}

void EditorUI::RenderSpriteFrameList(Api* api) {
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
    // 1. Identify deleted frames
    for (const auto& original : original_sprite_frames_) {
      bool found = false;
      for (const auto& current : current_sprite_frames_) {
        if (current.id == original.id) {
          found = true;
          break;
        }
      }
      if (!found) {
        LOG(INFO) << "Deleting SpriteFrame ID: " << original.id;
        auto status = api->DeleteSpriteFrame(original.id);
        if (!status.ok()) {
          LOG(ERROR) << "Failed to delete SpriteFrame " << original.id << ": " << status;
        }
      }
    }

    // 2. Insert or Update current frames
    for (int i = 0; i < current_sprite_frames_.size(); ++i) {
      SpriteFrame& frame = current_sprite_frames_[i];
      frame.index = i;  // Update index based on current order

      if (frame.id == 0) {
        // New frame
        LOG(INFO) << "Inserting new SpriteFrame at index " << i;
        auto result = api->InsertSpriteFrame(selected_sprite_id_, frame);
        if (result.ok()) {
          frame.id = *result;
        } else {
          LOG(ERROR) << "Failed to insert SpriteFrame: " << result.status();
        }
      } else {
        // Update existing
        // LOG(INFO) << "Updating SpriteFrame ID: " << frame.id << " at index " << i;
        auto result = api->UpdateSpriteFrame(frame);
        if (!result.ok()) {
          LOG(ERROR) << "Failed to update SpriteFrame: " << result;
        }
      }
    }

    LOG(INFO) << "Saved changes for sprite " << selected_sprite_id_;
    // Refresh original state
    original_sprite_frames_ = current_sprite_frames_;
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

void EditorUI::RenderSpriteFrameItem(size_t index, SpriteFrame& frame) {
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

  auto Clamp = [](int& val, int min, int max) {
    if (val < min) val = min;
    if (val > max) val = max;
  };

  ImGui::AlignTextToFramePadding();
  ImGui::Text("X:");
  ImGui::SameLine();
  if (ImGui::InputInt("##tx", &frame.texture_x)) {
    Clamp(frame.texture_x, 0, tex_w > 0 ? tex_w - frame.texture_w : 0);
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Y:");
  ImGui::SameLine();
  if (ImGui::InputInt("##ty", &frame.texture_y)) {
    Clamp(frame.texture_y, 0, tex_h > 0 ? tex_h - frame.texture_h : 0);
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("W:");
  ImGui::SameLine();
  if (ImGui::InputInt("##tw", &frame.texture_w)) {
    Clamp(frame.texture_w, 0, tex_w > 0 ? tex_w - frame.texture_x : 0);
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("H:");
  ImGui::SameLine();
  if (ImGui::InputInt("##th", &frame.texture_h)) {
    Clamp(frame.texture_h, 0, tex_h > 0 ? tex_h - frame.texture_y : 0);
  }

  // Render Offsets
  ImGui::Text("Render:");
  ImGui::AlignTextToFramePadding();
  ImGui::Text("X:");
  ImGui::SameLine();
  ImGui::InputInt("##rox", &frame.render_offset_x);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Y:");
  ImGui::SameLine();
  ImGui::InputInt("##roy", &frame.render_offset_y);

  ImGui::PopItemWidth();
  ImGui::PopID();
  ImGui::EndGroup();

  ImGui::SameLine();
  ImGui::Dummy(ImVec2(10, 0));
  ImGui::SameLine();
}

void EditorUI::RenderFullTextureView() {
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

void EditorUI::LoadSpriteTexture(int sprite_id) {
  // Load texture
  if (sprite_texture_) {
    SDL_DestroyTexture(sprite_texture_);
    sprite_texture_ = nullptr;
  }

  // Find the sprite config to get the path
  std::string path;
  for (const auto& s : sprite_list_) {
    if (s.id == sprite_id) {
      path = s.texture_path;
      break;
    }
  }

  if (!path.empty() && renderer_) {
    if (std::filesystem::exists(path)) {
      SDL_Surface* surface = IMG_Load(path.c_str());
      if (surface) {
        sprite_texture_ = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
      } else {
        LOG(ERROR) << "Failed to load sprite texture: " << IMG_GetError();
      }
    }
  }
}

void EditorUI::LoadTexturePreview(const std::string& path) {
  if (!renderer_) return;

  if (preview_texture_) {
    SDL_DestroyTexture(preview_texture_);
    preview_texture_ = nullptr;
  }

  // Check if file exists
  if (!std::filesystem::exists(path)) {
    LOG(ERROR) << "Texture file not found: " << path;
    return;
  }

  SDL_Surface* surface = IMG_Load(path.c_str());
  if (!surface) {
    LOG(ERROR) << "Failed to load texture surface: " << IMG_GetError();
    return;
  }

  preview_texture_ = SDL_CreateTextureFromSurface(renderer_, surface);
  SDL_FreeSurface(surface);

  if (!preview_texture_) {
    LOG(ERROR) << "Failed to create texture from surface: " << SDL_GetError();
  }
}

}  // namespace zebes
