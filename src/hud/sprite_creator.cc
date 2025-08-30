#include "hud/sprite_creator.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "common/status_macros.h"
#include "hud/texture_creator.h"
#include "hud/utils.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "objects/sprite_interface.h"

namespace zebes {

namespace {

ImVec2 GetSourceCoordinates(const HudTexture &hud_texture, Point coordinates) {
  return ImVec2(coordinates.x / hud_texture.width,
                coordinates.y / hud_texture.height);
}

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

absl::StatusOr<HudSpriteCreator> HudSpriteCreator::Create(
    const Options &options) {
  if (options.sprite_manager == nullptr)
    return absl::InvalidArgumentError("Sprite Manager must not be null.");
  if (options.texture_creator == nullptr)
    return absl::InvalidArgumentError("Texture Creator must not be null.");
  auto hud_sprite_creator = HudSpriteCreator(options);
  RETURN_IF_ERROR(hud_sprite_creator.Init());
  return hud_sprite_creator;
}

HudSpriteCreator::HudSpriteCreator(const Options &options)
    : sprite_manager_(options.sprite_manager),
      texture_creator_(options.texture_creator) {}

absl::Status HudSpriteCreator::Init() {
  const std::vector<Point> vertices = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};

  for (uint16_t sprite_id : sprite_manager_->GetAllSpriteIds()) {
    ObjectOptions options = {.object_type = ObjectType::kSprite,
                             .vertices = vertices,
                             .sprite_ids = {sprite_id}};

    ASSIGN_OR_RETURN(std::unique_ptr<SpriteObject> sprite_object,
                     SpriteObject::Create(options));

    ASSIGN_OR_RETURN(const SpriteInterface *sprite,
                     sprite_manager_->GetSprite(sprite_id));

    RETURN_IF_ERROR(sprite_object->AddSprite(sprite));
    RETURN_IF_ERROR(sprite_object->SetActiveSprite(sprite_id));

    auto hud_sprite = std::unique_ptr<HudSprite>(new HudSprite{
        .sprite_id = sprite_id, .sprite_object = std::move(sprite_object)});

    sprites_.insert({sprite_id, std::move(hud_sprite)});
  }

  return absl::OkStatus();
}

void HudSpriteCreator::RenderSpriteList() {
  if (ImGui::Button("Create New Sprite")) {
    editting_state_.sprite_id = UINT16_MAX;
    LOG(INFO) << "Creating new sprite...";
  }

  // Only set collapsed state in the case that the button is clicked. This will
  // apply to all sprite windows.
  std::optional<bool> expand_header;

  ImGui::SameLine();
  if (ImGui::Button("Collapse All##Sprites")) {
    LOG(INFO) << "Collapsing all sprite windows...";
    expand_header = false;
  }

  ImGui::SameLine();
  if (ImGui::Button("Expand All##Sprites")) {
    LOG(INFO) << "Expanding all sprite windows...";
    expand_header = true;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  for (auto &[_, sprite] : sprites_) {
    if (expand_header.has_value()) {
      ImGui::SetNextItemOpen(*expand_header);
    }
    if (ImGui::CollapsingHeader(sprite->label_main.c_str())) {
      RenderSpriteCollapse(draw_list, *sprite);
    }
  }
}

void HudSpriteCreator::RenderSpriteCollapse(ImDrawList *draw_list,
                                            HudSprite &sprite) {
  int active_index = sprite.sprite_object->GetActiveSpriteIndex();

  ImGui::BeginDisabled(true);
  int sprite_id = sprite.sprite_id;
  ImGui::InputInt(sprite.label_sprite_id.c_str(), &sprite_id);
  ImGui::EndDisabled();

  ImGui::InputText(sprite.label_type_name.c_str(),
                   sprite.hud_config.type_name.data(),
                   sizeof(sprite.hud_config.type_name));
  ImGui::InputInt(sprite.label_ticks_per_sprite.c_str(),
                  &sprite.hud_config.ticks_per_sprite);

  std::string apply_label = absl::StrCat("Apply##", sprite.unique_name);
  if (ImGui::Button(apply_label.c_str())) {
    LOG(INFO) << "Applying sprite changes...";
  }

  ImGui::SameLine();
  std::string edit_label = absl::StrCat("Edit Animation", sprite.unique_name);
  if (ImGui::Button(edit_label.c_str())) {
    editting_state_.sprite_id = sprite.sprite_id;
    LOG(INFO) << "Editting sprite animation...";
  }

  ImGui::SameLine();
  std::string pause_label = absl::StrCat("Pause", sprite.unique_name);
  if (ImGui::Button(pause_label.c_str())) {
    LOG(INFO) << "Pausing sprite animation...";
  }

  ImGui::SameLine();
  std::string delete_label = absl::StrCat("Delete", sprite.unique_name);
  if (ImGui::Button(delete_label.c_str())) {
    LOG(INFO) << "Deleting sprite...";
  }

  // Get active sprite from sprite object, there could be multiple sprites
  // per object
  const SpriteInterface *active_sprite =
      sprite.sprite_object->GetActiveSprite();

  // Get sub sprite from sprite for configuration information (height,
  // width)
  const SubSprite *active_sub_sprite =
      active_sprite->GetSubSprite(active_index);

  // Get texture from sprite
  SDL_Texture *active_texture = active_sprite->GetTextureCopy(active_index);

  // Render the texture to ImGui
  RenderTextureToImGui(active_texture, active_sub_sprite->render_w,
                       active_sub_sprite->render_h);
}

void HudSpriteCreator::RenderEditor() {
  // Reset any stateful values relating to rendered textures.
  texture_offset_.reset();
  texture_size_.reset();

  if (editting_state_.sprite_id == 0) {
    ImGui::Text("Create a new sprite or edit an existing sprite to begin.");
    return;
  }

  // Render texture selection drop down.
  ImGui::SetNextItemWidth(150.0f);
  int count = 0;
  const char **texture_names = texture_creator_->GetTextureNames(&count);
  ImGui::Combo("Texture Select", &editting_state_.texture_index, texture_names,
               count);

  if (editting_state_.texture_index == 0) {
    ImGui::Text("No texture selected.");
    return;
  }

  ImGui::NewLine();

  // Now that a texture has been selected, add sprite specific configurations.
  ImGui::BeginDisabled(true);
  ImGui::SetNextItemWidth(150.0f);
  int temp_sprite_id = editting_state_.sprite_id;
  ImGui::InputInt("Sprite ID", &temp_sprite_id);
  ImGui::EndDisabled();

  // Render ticks per sprite, this is a setting specific to the entire sprite.
  ImGui::SetNextItemWidth(150.0f);
  int temp_ticks_per_sprite = editting_state_.ticks_per_sprite;
  if (ImGui::InputInt("Ticks Per Sprite", &temp_ticks_per_sprite)) {
    editting_state_.ticks_per_sprite =
        std::clamp(temp_ticks_per_sprite, 0, 100);
  }

  // Render textbox to get user defined numerical id for this sprite.
  ImGui::SetNextItemWidth(150.0f);
  int temp_type = editting_state_.type;
  if (ImGui::InputInt("Type", &temp_type)) {
    editting_state_.type = std::clamp(temp_type, 0, UINT16_MAX);
  }

  // Render textbox to get user defined string id for this sprite.
  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputText("Type Name", editting_state_.type_name.data(),
                   sizeof(editting_state_.type_name));

  // Render checkbox to indicate if this sprite is interactible. If this is
  // interactible, it must have a defined hitbox.
  ImGui::Checkbox("Interactable", &editting_state_.interactable);

  // Now render the texture that has been selected. The user will be able to
  // select an area on the texture to specificy a specific sub sprite.
  absl::StatusOr<HudTexture *> texture_result =
      texture_creator_->FindTextureByIndex(editting_state_.texture_index);
  if (!texture_result.ok()) {
    ImGui::Text("Texture not found.");
    return;
  }
  HudTexture &hud_texture = *texture_result.value();

  // Next, render the button to add a subsprite. Each sprite requires at least
  // one subsprite.
  ImGui::NewLine();
  if (ImGui::Button("Add Sub Sprite")) {
    editting_state_.sub_sprites.push_back({
        .active = false,
        .index = editting_state_.sub_sprites.size(),
    });
  }

  // Next, render the button to delete a previously defined sub sprite.
  ImGui::SameLine();
  if (ImGui::Button("Delete Sub Sprite")) {
    if (!editting_state_.sub_sprites.empty()) {
      editting_state_.sub_sprites.pop_back();
    }
  }

  ImGui::NewLine();

  // Sub sprite settings.
  bool already_active = false;
  for (size_t i = 0; i < editting_state_.sub_sprites.size(); i++) {
    absl::Status result = RenderSubSpriteEditor(i, already_active);
    if (!result.ok()) {
      LOG(WARNING) << "Failed to render sub sprite editor: "
                   << result.message();
    }
  }

  if (editting_state_.zoom < 1) {
    editting_state_.zoom = 1;
  }

  ImGui::NewLine();
  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputInt("Texture Zoom", &editting_state_.zoom);
  ImGui::NewLine();

  // Create a temporary ImGui texture ID
  ImTextureID texture_id = (ImTextureID)(intptr_t)hud_texture.texture;
  texture_size_ = ImVec2(hud_texture.width * editting_state_.zoom,
                         hud_texture.height * editting_state_.zoom);

  // Position must be set before rendering the image.
  texture_offset_ = ImGui::GetCursorScreenPos();
  ImGui::Image(texture_id, *texture_size_);

  // Render the sprite editor selection.
  RenderSpriteEditorSelection();
}

void HudSpriteCreator::RenderSpriteEditorSelection() {
  if (!editting_state_.selection_start.has_value() ||
      !editting_state_.selection_end.has_value() ||
      !texture_offset_.has_value()) {
    return;
  }

  ImVec2 min = ImVec2(std::min(editting_state_.selection_start->x,
                               editting_state_.selection_end->x),
                      std::min(editting_state_.selection_start->y,
                               editting_state_.selection_end->y));
  ImVec2 max = ImVec2(std::max(editting_state_.selection_start->x,
                               editting_state_.selection_end->x),
                      std::max(editting_state_.selection_start->y,
                               editting_state_.selection_end->y));

  min.x = (min.x * editting_state_.zoom) + texture_offset_->x;
  min.y = (min.y * editting_state_.zoom) + texture_offset_->y;

  max.x = (max.x * editting_state_.zoom) + texture_offset_->x;
  max.y = (max.y * editting_state_.zoom) + texture_offset_->y;

  const float thickness = static_cast<float>(editting_state_.zoom);
  ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(0, 255, 0, 255), 0.0f,
                                      0, thickness);
}

absl::Status HudSpriteCreator::RenderSubSpriteEditor(int index,
                                                     bool &already_active) {
  // Get sub sprite from sprite index
  if (editting_state_.sub_sprites.size() <= index) {
    return absl::InvalidArgumentError("Sub sprite not found.");
  }
  EdittingSubSprite &edit_sub = editting_state_.sub_sprites[index];
  SubSprite &sub_sprite = edit_sub.sub_sprite;

  // If collapse header is not expanded, return early.
  const std::string label = absl::StrCat("Sub Sprite ", edit_sub.unique_name);
  if (!ImGui::CollapsingHeader(label.c_str())) {
    edit_sub.active = false;
    return absl::OkStatus();
  }
  edit_sub.active = !already_active && edit_sub.active;

  auto label_maker = [&](const std::string &name,
                         std::optional<int> id = std::nullopt) -> const char * {
    if (id.has_value()) {
      return strdup(absl::StrCat(name, edit_sub.unique_name, *id).c_str());
    }
    return strdup(absl::StrCat(name, edit_sub.unique_name).c_str());
  };

  ImGui::BeginColumns(label_maker("Columns"), 3);
  ImGui::Checkbox(label_maker("Active"), &edit_sub.active);
  ImGui::Checkbox(label_maker("Show Hitbox"), &edit_sub.show_hitbox);
  RETURN_IF_ERROR(RenderSubSpriteImage(edit_sub));
  ImGui::NextColumn();

  // Render next column which consists of coordinate settings for this
  // subsprite.
  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputInt(label_maker("Sub Sprite Source X"), &sub_sprite.texture_x);

  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputInt(label_maker("Sub Sprite Source Y"), &sub_sprite.texture_y);

  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputInt(label_maker("Sub Sprite Source Width"),
                  &sub_sprite.texture_w);

  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputInt(label_maker("Sub Sprite Source Height"),
                  &sub_sprite.texture_h);

  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputInt(label_maker("Sub Sprite Render Width"), &sub_sprite.render_w);

  ImGui::SetNextItemWidth(150.0f);
  ImGui::InputInt(label_maker("Sub Sprite Render Height"),
                  &sub_sprite.render_h);

  // Render next column which consists of hitbox related settings.
  ImGui::NextColumn();

  // If this sprite is not interactable, do not render hitbox related settings.
  ImGui::Text("Hitbox");
  if (!editting_state_.interactable) {
    ImGui::Text("Interactable must be enabled to edit hitbox.");
    ImGui::EndColumns();
    already_active |= edit_sub.active;
    return absl::OkStatus();
  }

  // If the hitbox is "empty", populate default hitbox.
  if (edit_sub.hitbox.empty()) {
    edit_sub.hitbox.push_back({.x = 0, .y = 0});
    edit_sub.hitbox.push_back(
        {.x = static_cast<double>(sub_sprite.texture_w), .y = 0});
    edit_sub.hitbox.push_back({.x = static_cast<double>(sub_sprite.texture_w),
                               .y = static_cast<double>(sub_sprite.texture_h)});
    edit_sub.hitbox.push_back(
        {.x = 0, .y = static_cast<double>(sub_sprite.texture_h)});
  }

  // Add point to hitbox, typically you should have four points per hitbox.
  // Additinally, the shape must be entirely compose of convex vertices.
  if (ImGui::Button("Add Point")) {
    edit_sub.hitbox.push_back({0, 0});
  }

  // Remove point from hitbox.
  ImGui::SameLine();
  if (ImGui::Button("Remove Point")) {
    if (edit_sub.hitbox.size() > 3) {
      edit_sub.hitbox.pop_back();
    } else {
      LOG(WARNING) << "Cannot remove point, must have at least 3 points.";
    }
  }

  // Ensure that there is no less than three points per hitbox.
  while (edit_sub.hitbox.size() < 3) {
    edit_sub.hitbox.push_back({.x = 0, .y = 0});
  }

  // Create inputs for all points within the hitbox.
  for (size_t j = 0; j < edit_sub.hitbox.size(); j++) {
    Point &point = edit_sub.hitbox[j];
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputDouble(label_maker("X", j), &point.x);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputDouble(label_maker("Y", j), &point.y);
  }

  // Make sure this is the currently active sub sprite.
  ImGui::EndColumns();
  already_active |= edit_sub.active;
  return absl::OkStatus();
}

absl::Status HudSpriteCreator::RenderSubSpriteImage(
    const EdittingSubSprite &edit_sub) {
  // Get texture from texture index
  ASSIGN_OR_RETURN(
      HudTexture * hud_texture,
      texture_creator_->FindTextureByIndex(editting_state_.texture_index));

  const SubSprite &sub_sprite = edit_sub.sub_sprite;
  if (sub_sprite.texture_w <= 0 || sub_sprite.texture_h <= 0 ||
      sub_sprite.render_w <= 0 || sub_sprite.render_h <= 0) {
    ImGui::Text("Image Goes Here");
    return absl::OkStatus();
  }

  // Get the current offset position. This is required so that we know
  const ImVec2 offset_position = ImGui::GetCursorScreenPos();
  const ImTextureID texture_id = (ImTextureID)(intptr_t)hud_texture->texture;

  const ImVec2 render_size =
      ImVec2(sub_sprite.texture_w * editting_state_.zoom,
             sub_sprite.texture_h * editting_state_.zoom);

  const ImVec2 source_min = GetSourceCoordinates(
      *hud_texture, {.x = static_cast<double>(sub_sprite.texture_x),
                     .y = static_cast<double>(sub_sprite.texture_y)});

  const ImVec2 source_max = GetSourceCoordinates(
      *hud_texture,
      {.x = static_cast<double>(sub_sprite.texture_x + sub_sprite.texture_w),
       .y = static_cast<double>(sub_sprite.texture_y + sub_sprite.texture_h)});

  ImGui::Image(texture_id, render_size, source_min, source_max);

  if (!edit_sub.show_hitbox) {
    return absl::OkStatus();
  }

  for (size_t i = 0; i < edit_sub.hitbox.size(); i++) {
    const Point &p1 = edit_sub.hitbox[i];
    const Point &p2 = edit_sub.hitbox[(i + 1) % edit_sub.hitbox.size()];

    ImVec2 p1i = ImVec2(p1.x * editting_state_.zoom + offset_position.x,
                        p1.y * editting_state_.zoom + offset_position.y);
    ImVec2 p2i = ImVec2(p2.x * editting_state_.zoom + offset_position.x,
                        p2.y * editting_state_.zoom + offset_position.y);

    float thickness = static_cast<float>(editting_state_.zoom);
    ImGui::GetWindowDrawList()->AddLine(p1i, p2i, IM_COL32(0, 255, 0, 255),
                                        thickness);
  }

  return absl::OkStatus();
}

void HudSpriteCreator::Update() {
  // Update all sprite objects.
  for (auto &[_, sprite] : sprites_) {
    sprite->sprite_object->Update();
  }

  if (!texture_offset_.has_value() || !texture_size_.has_value()) {
    return;
  }

  if (!IsMouseOverRect(*texture_offset_, *texture_size_)) {
    return;
  }

  if (ImGui::IsMouseClicked(0)) {
    editting_state_.selection_start =
        GetRelativePosition(*texture_offset_, editting_state_.zoom);
    editting_state_.selection_end = editting_state_.selection_start;
    return;
  }

  if (ImGui::IsMouseDown(0)) {
    editting_state_.selection_end =
        GetRelativePosition(*texture_offset_, editting_state_.zoom);
    return;
  }

  // If the selection is still held return.
  if (!ImGui::IsMouseReleased(0)) {
    return;
  }

  // Here we are going to adjust the selection to the nearest integer.
  if (editting_state_.selection_start.has_value()) {
    editting_state_.selection_start->x =
        std::round(editting_state_.selection_start->x);
    editting_state_.selection_start->y =
        std::round(editting_state_.selection_start->y);
  }

  if (editting_state_.selection_end.has_value()) {
    editting_state_.selection_end->x =
        std::round(editting_state_.selection_end->x);
    editting_state_.selection_end->y =
        std::round(editting_state_.selection_end->y);
  }

  // At this point there should be an active index.
  const int active_index = editting_state_.GetActiveIndex();
  if (active_index < 0) {
    return;
  }

  // Upsert the subsprite for the active index.
  SubSprite &sub_sprite = editting_state_.sub_sprites[active_index].sub_sprite;
  sub_sprite.texture_x = editting_state_.selection_start->x;
  sub_sprite.texture_y = editting_state_.selection_start->y;
  sub_sprite.texture_w =
      editting_state_.selection_end->x - editting_state_.selection_start->x;
  sub_sprite.texture_h =
      editting_state_.selection_end->y - editting_state_.selection_start->y;
  sub_sprite.render_w = sub_sprite.texture_w;
  sub_sprite.render_h = sub_sprite.texture_h;
}

}  // namespace zebes
