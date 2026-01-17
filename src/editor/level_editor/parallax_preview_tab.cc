#include "editor/level_editor/parallax_preview_tab.h"

#include "absl/status/status.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/imgui_scoped.h"

namespace zebes {

ParallaxPreviewTab::ParallaxPreviewTab(Api& api, GuiInterface* gui) : api_(api), gui_(gui) {}

void ParallaxPreviewTab::Reset() {
  zoom_ = 1.0f;
  preview_w_ = 512;
  preview_h_ = 0;
}

absl::Status ParallaxPreviewTab::Render(std::optional<std::string> texture_id) {
  std::string tab_name = "Parallax Layer";

  auto tab_item = ScopedTabItem(gui_, tab_name.c_str());
  if (!tab_item) return absl::OkStatus();

  RenderZoom();

  if (!texture_id.has_value()) {
    gui_->Text("No texture selected...");
    return absl::OkStatus();
  }

  gui_->Text("Texture ID: %s", texture_id->c_str());
  absl::StatusOr<Texture*> texture_or = api_.GetTexture(*texture_id);
  if (!texture_or.ok()) {
    gui_->Text("Error: %s", std::string(texture_or.status().message()).c_str());
    return absl::OkStatus();
  }
  const Texture& texture = *(*texture_or);

  if (texture.sdl_texture == nullptr) {
    gui_->Text("Error: SDL_Texture is null.");
    return absl::OkStatus();
  }

  int w = 0, h = 0;
  SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(texture.sdl_texture), nullptr, nullptr, &w, &h);
  float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
  float preview_w = 200.0f * zoom_;
  float preview_h = preview_w / aspect;

  gui_->Text("Size: %dx%d", w, h);

  gui_->Image(reinterpret_cast<ImTextureID>(texture.sdl_texture), ImVec2(preview_w, preview_h));

  return absl::OkStatus();
}

void ParallaxPreviewTab::RenderZoom() {
  // Zoom controls
  if (gui_->Button("-")) {
    zoom_ *= 0.8f;
    if (zoom_ < 0.1f) zoom_ = 0.1f;
  }
  gui_->SameLine();
  if (gui_->Button("+")) {
    zoom_ *= 1.25f;
    if (zoom_ > 10.0f) zoom_ = 10.0f;
  }
  gui_->SameLine();
  if (gui_->Button("Reset Zoom")) {
    zoom_ = 1.0f;
  }
  gui_->SameLine();
  gui_->Text("Zoom: %.1fx", zoom_);
}

}  // namespace zebes
