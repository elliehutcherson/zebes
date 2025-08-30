#include "texture_creator.h"

#include <optional>

#include "ImGuiFileDialog.h"
#include "SDL_render.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "hud/utils.h"
#include "imgui.h"

namespace zebes {
namespace {

void RenderTextureToImGui(SDL_Texture *texture, int width, int height) {
  // Create a temporary ImGui texture ID
  ImTextureID texture_id = (ImTextureID)(intptr_t)texture;
  ImVec2 size = ImVec2(width, height);

  // Position must be set before rendering the image.
  ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::Image(texture_id, size);

  // Check if the texture was clicked.
  if (IsMouseOverRect(position, size) && ImGui::IsMouseClicked(0)) {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mouse_position = io.MousePos;
    LOG(INFO) << absl::StrFormat("Texture mouse down, x: %f, y: %f",
                                 mouse_position.x, mouse_position.y);
  }

  if (IsMouseOverRect(position, size) && ImGui::IsMouseReleased(0)) {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mouse_position = io.MousePos;
    LOG(INFO) << absl::StrFormat("Texture mouse down, x: %f, y: %f",
                                 mouse_position.x, mouse_position.y);
  }
}

}  // namespace

absl::StatusOr<HudTextureCreator> HudTextureCreator::Create(
    const HudTextureCreator::Options &options) {
  if (options.sprite_manager == nullptr) {
    return absl::InvalidArgumentError("Sprite Manager must not be null.");
  }
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("API must not be null.");
  }
  return HudTextureCreator(options);
}

HudTextureCreator::HudTextureCreator(const Options &options)
    : sprite_manager_(options.sprite_manager),
      api_(options.api) {
  texture_names_.push_back("None");
}

absl::StatusOr<HudTexture *> HudTextureCreator::FindTextureByIndex(int index) {
  if (index < 0 || index >= texture_names_.size()) {
    return absl::InvalidArgumentError("Texture index out of bounds.");
  }
  auto it = name_to_textures_.find(texture_names_[index]);
  if (it == name_to_textures_.end()) {
    return absl::InvalidArgumentError("Texture not found.");
  }
  return &it->second;
}

const char **HudTextureCreator::GetTextureNames(int* count) {
  *count = texture_names_.size();
  return texture_names_.data();
}

void HudTextureCreator::Render() {
  std::string label = absl::StrCat("##TextureImport");
  ImGui::Text("Import Path: ");
  ImGui::SameLine();

  ImGui::InputText(label.c_str(), texture_import_path_,
                   sizeof(texture_import_path_), ImGuiInputTextFlags_ReadOnly);
  ImGui::SameLine();

  // Add Browse button
  if (ImGui::Button("Browse")) {
    show_file_dialog_ = true;
  }
  ImGui::SameLine();

  // Add Import button
  if (ImGui::Button("Import")) {
    absl::Status status = ImportTexture();
    if (!status.ok()) {
      LOG(WARNING) << "Failed to import texture: " << status.message();
    }
  }

  // Handle file dialog
  if (show_file_dialog_) {
    IGFD::FileDialogConfig config;
    config.path = ".";
    ImGuiFileDialog::Instance()->OpenDialog(
        "ChooseFileDlgKey", "Choose File", ".png,.jpg,.jpeg,.bmp,.tga", config);
  }

  // Display file dialog
  if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      temp_texture_path_ = ImGuiFileDialog::Instance()->GetFilePathName();
      strncpy(texture_import_path_, temp_texture_path_->c_str(),
              sizeof(texture_import_path_) - 1);
      texture_import_path_[sizeof(texture_import_path_) - 1] = '\0';

      absl::Status status = sprite_manager_->AddTexture(*temp_texture_path_);
      if (!status.ok()) {
        LOG(WARNING) << "Failed to find texture: " << status.message();
        temp_texture_path_ = std::nullopt;
      }
    }
    ImGuiFileDialog::Instance()->Close();
    show_file_dialog_ = false;
  }

  int texture_index = 0;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  if (temp_texture_path_.has_value() && temp_texture_.has_value()) {
    RenderTexture(draw_list, *temp_texture_, -1, /*render_collpase=*/false);
  }

  for (auto &[_, hud_texture] : name_to_textures_) {
    RenderTexture(draw_list, hud_texture, texture_index++);
  }
}

void HudTextureCreator::RenderTexture(ImDrawList *draw_list,
                                      HudTexture &hud_texture,
                                      int texture_index, bool render_collpase) {
  std::string label = absl::StrCat("##TextureSettings", texture_index);
  if (render_collpase && !ImGui::CollapsingHeader(label.c_str())) return;

  // Get current cursor position
  const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  // Render the texture to ImGui
  RenderTextureToImGui(hud_texture.texture, hud_texture.width,
                       hud_texture.height);

  // Calculate rectangle position and size
  const ImVec2 rect_min = cursor_pos;  // Top-left corner
  const ImVec2 rect_max =
      ImVec2(cursor_pos.x + hud_texture.width,
             cursor_pos.y + hud_texture.height);  // Bottom-right corner

  // Draw rectangle around the texture
  draw_list->AddRect(rect_min, rect_max, IM_COL32(0, 255, 0, 255), 0.0f, 0,
                     2.0f);  // Green border, 2px thick
}

void HudTextureCreator::Update() {
  if ((sprite_manager_->GetAllTextures()->size() + 1) == name_to_textures_.size()) {
    return;
  }

  name_to_textures_.clear();
  texture_names_.clear();
  texture_names_.push_back("None");

  for (auto &[path, texture] : *sprite_manager_->GetAllTextures()) {
    int width = 0;
    int height = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);

    const std::string name = path.substr(path.find_last_of('/') + 1);
    HudTexture hud_texture = {.texture = texture,
                              .name = name,
                              .path = path,
                              .width = width,
                              .height = height};

    if (temp_texture_path_.has_value() && *temp_texture_path_ == path) {
      temp_texture_ = hud_texture;
      continue;
    }
    texture_names_.push_back(strdup(hud_texture.name.c_str()));
    name_to_textures_.insert({name, std::move(hud_texture)});
  }
}

absl::Status HudTextureCreator::ImportTexture() {
  if (!temp_texture_path_.has_value()) {
    return absl::InvalidArgumentError("No texture path selected.");
  }

  absl::Status status = sprite_manager_->DeleteTexture(*temp_texture_path_);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to delete temp texture: " << status.message();
  }

  LOG(INFO) << "Importing texture from path: " << *temp_texture_path_;

  absl::StatusOr<std::string> result =
      api_->CreateTexture(*temp_texture_path_);
  if (!result.ok()) {
    return absl::InternalError(
        absl::StrCat("Failed to import texture: ", result.status().message()));
  }

  temp_texture_path_ = std::nullopt;
  temp_texture_ = std::nullopt;

  LOG(INFO) << "Successfully imported texture, path: " << *result;
  return absl::OkStatus();
}

}  // namespace zebes
