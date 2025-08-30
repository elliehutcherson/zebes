#include "hud.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SDL_render.h"
#include "SDL_video.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "engine/config.h"
#include "engine/controller.h"
#include "engine/logging.h"
#include "engine/sprite_interface.h"
#include "engine/sprite_object.h"
#include "engine/status_macros.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"

namespace zebes {
namespace {

ImVec2 GetRelativePosition(const ImVec2 &offset_position, int scale) {
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 mouse_position = io.MousePos;
  ImVec2 relative_position;
  relative_position.x = (mouse_position.x - offset_position.x) / scale;
  relative_position.y = (mouse_position.y - offset_position.y) / scale;
  return relative_position;
}

bool IsMouseOverRect(const ImVec2 &position, const ImVec2 &size) {
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 mouse_position = io.MousePos;
  return mouse_position.x >= position.x &&
         mouse_position.x <= position.x + size.x &&
         mouse_position.y >= position.y &&
         mouse_position.y <= position.y + size.y;
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

ImVec2 Hud::GetSourceCoordinates(const HudTexture &hud_texture,
                                 Point coordinates) {
  return ImVec2(coordinates.x / hud_texture.width,
                coordinates.y / hud_texture.height);
}

absl::StatusOr<std::unique_ptr<Hud>> Hud::Create(Hud::Options options) {
  std::unique_ptr<Hud> hud(new Hud(options));
  if (options.config == nullptr)
    return absl::InvalidArgumentError("Config must not be null.");
  if (options.focus == nullptr)
    return absl::InvalidArgumentError("Focus must not be null.");
  if (options.controller == nullptr)
    return absl::InvalidArgumentError("Controller must not be null.");
  if (options.sprite_manager == nullptr)
    return absl::InvalidArgumentError("SpriteManager must not be null.");

  RETURN_IF_ERROR(hud->Init(options.window, options.renderer));
  return hud;
}

Hud::Hud(Options options)
    : config_(options.config),
      focus_(options.focus),
      controller_(options.controller),
      sprite_manager_(options.sprite_manager) {}

absl::Status Hud::Init(SDL_Window *window, SDL_Renderer *renderer) {
  renderer_ = renderer;

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imgui_io_ = &ImGui::GetIO();
  imgui_io_->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);
  clear_color_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  std::vector<Point> vertices = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};

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

void Hud::InjectEvents() {
  bool is_main_window_focused = !ImGui::GetIO().WantCaptureMouse;
  if (is_main_window_focused) return;
  controller_->AddInternalEvent(
      {.type = InternalEventType::kIsMainWindowFocused,
       .value = is_main_window_focused});
}

void Hud::Update() {
  int index = 0;
  std::erase_if(scenes_, [&](const Scene &scene) {
    return removed_scenes_.contains(index++);
  });
  removed_scenes_.clear();

  for (auto &[_, sprite] : sprites_) {
    sprite->sprite_object->Update();
  }

  if (sprite_manager_->GetAllTextures()->size() != name_to_textures_.size()) {
    name_to_textures_.clear();
    texture_names_.clear();
    texture_names_.push_back("None");
    for (auto &[path, texture] : *sprite_manager_->GetAllTextures()) {
      int width, height;
      SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);

      std::string name = path.substr(path.find_last_of('/') + 1);
      HudTexture hud_texture = {.texture = texture,
                                .name = name,
                                .path = path,
                                .width = width,
                                .height = height};
      texture_names_.push_back(strdup(hud_texture.name.c_str()));
      name_to_textures_.insert({name, std::move(hud_texture)});
    }
  }

  UpdateSpriteEditorSelection();
}

void Hud::UpdateSpriteEditorSelection() {
  if (!sprite_editor_texture_offset_.has_value() ||
      !sprite_editor_texture_size_.has_value()) {
    return;
  }

  if (!IsMouseOverRect(*sprite_editor_texture_offset_,
                       *sprite_editor_texture_size_)) {
    return;
  }

  if (ImGui::IsMouseClicked(0)) {
    editting_state_.selection_start = GetRelativePosition(
        *sprite_editor_texture_offset_, editting_state_.zoom);
    editting_state_.selection_end = editting_state_.selection_start;
    return;
  }

  if (ImGui::IsMouseDown(0)) {
    editting_state_.selection_end = GetRelativePosition(
        *sprite_editor_texture_offset_, editting_state_.zoom);
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

void Hud::Render() {
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  imgui_context_ = ImGui::GetCurrentContext();

  if (config_->mode == GameConfig::Mode::kCreatorMode) {
    RenderCreatorMode();
  } else if (config_->mode == GameConfig::Mode::kPlayerMode) {
    RenderPlayerMode();
  }

  // End of frame: render Dear ImGui
  ImGui::Render();
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

void Hud::RenderPlayerMode() {
  ImGui::Begin("Player Mode");

  ImGui::Text("%s", focus_->to_string().c_str());

  ImGui::End();
}

void Hud::RenderWindowConfig() {
  static bool full_screen = hud_config_.window.full_screen();
  ImGui::PushItemWidth(100);
  ImGui::InputInt("window_width", &hud_config_.window.width);

  ImGui::PushItemWidth(100);
  ImGui::InputInt("window_height", &hud_config_.window.height);

  ImGui::PushItemWidth(100);
  if (ImGui::Checkbox("FullScreen ", &full_screen)) {
    hud_config_.window.flags = full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    LOG(INFO) << "Setting full screen to: " << full_screen;
  }

  if (ImGui::Button("Apply")) {
    SaveConfig();
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting window config...";
    hud_config_.window = config_->window;
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default window config...";
    hud_config_.window = WindowConfig();
  }
}

void Hud::RenderBoundaryConfig() {
  ImGui::InputInt("boundary_x_min", &hud_config_.boundaries.x_min);
  ImGui::InputInt("boundary_x_max", &hud_config_.boundaries.x_max);
  ImGui::InputInt("boundary_y_min", &hud_config_.boundaries.y_min);
  ImGui::InputInt("boundary_y_max", &hud_config_.boundaries.y_max);
  if (ImGui::Button("Apply")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting boundary config...";
    hud_config_.boundaries = config_->boundaries;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default boundary config...";
    hud_config_.boundaries = BoundaryConfig();
  }
}

void Hud::RenderTileConfig() {
  ImGui::InputInt("tile_scale", &hud_config_.tiles.scale);
  ImGui::InputInt("tile_source_width", &hud_config_.tiles.source_width);
  ImGui::InputInt("tile_source_height", &hud_config_.tiles.source_height);
  ImGui::InputInt("tile_size_x", &hud_config_.tiles.size_x);
  ImGui::InputInt("tile_size_y", &hud_config_.tiles.size_y);
  ImGui::Text("tile_render_width: %d",
              hud_config_.tiles.source_width * hud_config_.tiles.scale);
  ImGui::Text("tile_render_height: %d",
              hud_config_.tiles.source_height * hud_config_.tiles.scale);

  if (ImGui::Button("Apply")) {
    SaveConfig();
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting tile config...";
    hud_config_.tiles = config_->tiles;
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default tile config...";
    hud_config_.tiles = TileConfig();
  }
}

void Hud::RenderCollisionConfig() {
  ImGui::InputFloat("area_width", &hud_config_.collisions.area_width);
  ImGui::InputFloat("area_height", &hud_config_.collisions.area_height);
  if (ImGui::Button("Apply")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    LOG(INFO) << "Reseting tile config...";
    hud_config_.collisions = config_->collisions;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to Default")) {
    LOG(INFO) << "Reseting to default tile config...";
    hud_config_.collisions = CollisionConfig();
  }
}

void Hud::RenderSceneWindowPrimary() {
  if (ImGui::Button("Add Scene")) {
    int index = scenes_.size();
    scenes_.push_back({.index = index});
  }

  for (int i = 0; i < scenes_.size(); i++) {
    if (ImGui::CollapsingHeader(absl::StrFormat("Scene %d", i).c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      RenderSceneWindow(i);
    }
  }
}

void Hud::RenderSceneWindow(int index) {
  Scene &scene = scenes_[index];
  scene.index = index;
  ImGui::PushID(index);
  ImGui::Text("Scene %d", scene.index);
  ImGui::PopID();

  std::string label = absl::StrCat("##SceneSave", scene.index);
  ImGui::Text("Save Path: ");
  ImGui::SameLine();

  ImGui::InputText(label.c_str(), scene.save_path, 4096);
  ImGui::SameLine();

  if (ImGui::Button("Save")) {
    SaveConfig();
  }

  label = absl::StrCat("##SceneImport", scene.index);
  ImGui::Text("Import Path: ");
  ImGui::SameLine();

  ImGui::InputText(label.c_str(), scene.import_path, 4096);
  ImGui::SameLine();

  if (ImGui::Button("Import")) {
    LOG(INFO) << "Importing layer..." << std::endl;
  }

  if (ImGui::Button("Remove Scene")) {
    if (scene.index == active_scene_) {
      active_scene_ = -1;
    }
    removed_scenes_.insert(scene.index);
  }

  ImGui::SameLine();

  label = "Activate";
  bool pop_color = false;
  if (scene.index == active_scene_) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.4f, 0.8f, 0.4f, 1.0f));  // Greenish button
    label = "Deactivate";
    pop_color = true;
  }

  if (ImGui::Button(label.c_str())) {
    active_scene_ = scene.index;
  }

  if (pop_color) {
    ImGui::PopStyleColor();
  }
}

void Hud::RenderTexturesWindow() {
  std::string label = absl::StrCat("##TextureImport");
  ImGui::Text("Import Path: ");
  ImGui::SameLine();

  ImGui::InputText(label.c_str(), texture_import_path_,
                   sizeof(texture_import_path_));
  ImGui::SameLine();

  if (ImGui::Button("Import")) {
    std::string path = std::string(texture_import_path_);
    LOG(INFO) << "Importing texture from path: " << path;

    absl::Status status =
        sprite_manager_->AddTexture(std::string(texture_import_path_));

    if (!status.ok()) {
      LOG(WARNING) << "Failed to import texture: " << status.message();
    } else {
      LOG(INFO) << "Successfully imported texture.";
    }
  }

  int texture_index = 0;
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  for (auto &[_, hud_texture] : name_to_textures_) {
    RenderTextureCollapse(draw_list, hud_texture, texture_index++);
  }
}

void Hud::RenderTextureCollapse(ImDrawList *draw_list, HudTexture &hud_texture,
                                int texture_index) {
  std::string label = absl::StrCat("##TextureSettings", texture_index);
  if (!ImGui::CollapsingHeader(label.c_str())) return;

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

void Hud::RenderSpritesWindow() {
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

void Hud::RenderSpriteCollapse(ImDrawList *draw_list, HudSprite &sprite) {
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

void Hud::RenderSpriteEditor() {
  // Reset any stateful values relating to rendered textures.
  sprite_editor_texture_offset_.reset();
  sprite_editor_texture_size_.reset();

  if (editting_state_.sprite_id == 0) {
    ImGui::Text("Create a new sprite or edit an existing sprite to begin.");
    return;
  }

  // Render texture selection drop down.
  ImGui::SetNextItemWidth(150.0f);
  const char **texture_names = texture_names_.data();
  ImGui::Combo("Texture Select", &editting_state_.texture_index, texture_names,
               sizeof(texture_names));

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
      FindTextureByIndex(editting_state_.texture_index);
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
  sprite_editor_texture_size_ =
      ImVec2(hud_texture.width * editting_state_.zoom,
             hud_texture.height * editting_state_.zoom);

  // Position must be set before rendering the image.
  sprite_editor_texture_offset_ = ImGui::GetCursorScreenPos();
  ImGui::Image(texture_id, *sprite_editor_texture_size_);

  // Render the sprite editor selection.
  RenderSpriteEditorSelection();
}

void Hud::RenderSpriteEditorSelection() {
  if (!editting_state_.selection_start.has_value() ||
      !editting_state_.selection_end.has_value() ||
      !sprite_editor_texture_offset_.has_value()) {
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

  min.x = (min.x * editting_state_.zoom) + sprite_editor_texture_offset_->x;
  min.y = (min.y * editting_state_.zoom) + sprite_editor_texture_offset_->y;

  max.x = (max.x * editting_state_.zoom) + sprite_editor_texture_offset_->x;
  max.y = (max.y * editting_state_.zoom) + sprite_editor_texture_offset_->y;

  const float thickness = static_cast<float>(editting_state_.zoom);
  ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(0, 255, 0, 255), 0.0f,
                                      0, thickness);
}

absl::Status Hud::RenderSubSpriteEditor(int index, bool &already_active) {
  // Get texture from texture index
  ASSIGN_OR_RETURN(HudTexture * hud_texture,
                   FindTextureByIndex(editting_state_.texture_index));

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

  if (sub_sprite.texture_w <= 0 || sub_sprite.texture_h <= 0 ||
      sub_sprite.render_w <= 0 || sub_sprite.render_h <= 0) {
    ImGui::Text("Image Goes Here");
  } else {
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
         .y =
             static_cast<double>(sub_sprite.texture_y + sub_sprite.texture_h)});

    ImGui::Image(texture_id, render_size, source_min, source_max);

    if (edit_sub.show_hitbox) {
      for (size_t i = 0; i < edit_sub.hitbox.size(); i++) {
        Point &p1 = edit_sub.hitbox[i];
        Point &p2 = edit_sub.hitbox[(i + 1) % edit_sub.hitbox.size()];

        ImVec2 p1i = ImVec2(p1.x * editting_state_.zoom + offset_position.x,
                            p1.y * editting_state_.zoom + offset_position.y);
        ImVec2 p2i = ImVec2(p2.x * editting_state_.zoom + offset_position.x,
                            p2.y * editting_state_.zoom + offset_position.y);

        float thickness = static_cast<float>(editting_state_.zoom);
        ImGui::GetWindowDrawList()->AddLine(p1i, p2i, IM_COL32(0, 255, 0, 255),
                                            thickness);
      }
    }
  }
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

void Hud::RenderTerminalWindow() {
  ImGui::BeginChild("Log");
  ImGui::TextUnformatted(HudLogSink::Get()->log()->c_str());
  if (log_size_ != HudLogSink::Get()->log()->size()) {
    ImGui::SetScrollHereY(1.0f);
    log_size_ = HudLogSink::Get()->log()->size();
  }
  ImGui::EndChild();
}

void Hud::RenderCreatorMode() {
  bool open = true;

  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  ImGui::Begin("DockSpace Demo", &open, window_flags);
  ImGui::PopStyleVar(3);

  ImGuiID dockspace_id = ImGui::GetID("dockspace_id");
  if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr || rebuild_docks_) {
    ImGui::DockBuilderRemoveNode(dockspace_id);  // Clear out existing layout
    ImGui::DockBuilderAddNode(dockspace_id,
                              ImGuiDockNodeFlags_None);  // Add empty node

    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);
    ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
    ImGuiID dock_up_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Up, 0.05f, nullptr, &dock_main_id);
    ImGuiID dock_down_id = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Down, 0.2f, nullptr, &dock_main_id);

    ImGui::DockBuilderDockWindow("Scene", dock_main_id);
    ImGui::DockBuilderDockWindow("Config", dock_left_id);
    ImGui::DockBuilderDockWindow("Assets", dock_right_id);
    ImGui::DockBuilderDockWindow("Editor", dock_up_id);
    ImGui::DockBuilderDockWindow("Console", dock_down_id);

    ImGui::DockBuilderFinish(dockspace_id);
    rebuild_docks_ = false;
  }

  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), 0);
  ImGui::End();

  // Left side of the dock
  ImGui::Begin("Config", &open, 0);
  if (ImGui::CollapsingHeader("Camera Info")) {
    ImGui::Text("%s", focus_->to_string().c_str());
  }
  if (ImGui::CollapsingHeader("Window Config")) {
    RenderWindowConfig();
  }
  if (ImGui::CollapsingHeader("Boundary Config")) {
    RenderBoundaryConfig();
  }
  if (ImGui::CollapsingHeader("Tile Config")) {
    RenderTileConfig();
  }
  if (ImGui::CollapsingHeader("Collision Config")) {
    RenderCollisionConfig();
  }
  if (ImGui::CollapsingHeader("Scene Creation")) {
    RenderSceneWindowPrimary();
  }
  ImGui::End();

  // Right side of the dock
  ImGui::Begin("Assets", &open, 0);
  if (ImGui::CollapsingHeader("Textures")) {
    RenderTexturesWindow();
  }
  if (ImGui::CollapsingHeader("Sprites")) {
    RenderSpritesWindow();
  }
  ImGui::End();

  // Top side of the dock
  ImGui::Begin("Editor", &open, 0);
  RenderSpriteEditor();
  ImGui::End();

  // Bottom side of the dock
  ImGui::Begin("Console", &open, 0);
  if (ImGui::CollapsingHeader("Termainal")) {
    RenderTerminalWindow();
  }
  ImGui::End();
}

void Hud::SaveConfig() {
  LOG(INFO) << "Saving creator state..." << std::endl;
  controller_->AddInternalEvent(
      {.type = InternalEventType::kCreatorSaveConfig, .value = hud_config_});
}

}  // namespace zebes
