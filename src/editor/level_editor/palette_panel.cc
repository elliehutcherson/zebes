#include "editor/level_editor/palette_panel.h"

#include "absl/memory/memory.h"
#include "common/status_macros.h"
#include "editor/imgui_scoped.h"
#include "imgui.h"

namespace zebes {

namespace {

constexpr ImVec4 kActiveButtonColor = {0.3f, 0.6f, 1.0f, 1.0f};
constexpr ImVec4 kInactiveButtonColor = {0.26f, 0.26f, 0.26f, 1.0f};

}  // namespace

absl::StatusOr<std::unique_ptr<PalettePanel>> PalettePanel::Create(Options options) {
  if (options.api == nullptr) {
    return absl::InvalidArgumentError("Api must not be null.");
  }
  if (options.gui == nullptr) {
    return absl::InvalidArgumentError("Gui must not be null.");
  }

  auto panel = absl::WrapUnique(new PalettePanel());
  panel->gui_ = options.gui;

  if (options.blueprint_panel) {
    panel->blueprint_panel_ = std::move(options.blueprint_panel);
  } else {
    ASSIGN_OR_RETURN(panel->blueprint_panel_,
                     BlueprintPalettePanel::Create({.api = options.api, .gui = options.gui}));
  }

  if (options.tile_panel) {
    panel->tile_panel_ = std::move(options.tile_panel);
  } else {
    ASSIGN_OR_RETURN(panel->tile_panel_,
                     TilePalettePanel::Create({.api = options.api, .gui = options.gui}));
  }

  return panel;
}

absl::Status PalettePanel::Render(int tile_render_width, int tile_render_height) {
  // Each button is wrapped in a block so the ScopedStyleColor pops before
  // SameLine() and the next button are rendered.
  {
    ScopedStyleColor color = gui_->CreateScopedStyleColor(
        ImGuiCol_Button, mode_ == Mode::kBlueprints ? kActiveButtonColor : kInactiveButtonColor);
    if (gui_->Button("Blueprints")) {
      mode_ = Mode::kBlueprints;
      tile_panel_->ClearSelection();
    }
  }

  gui_->SameLine();

  {
    ScopedStyleColor color = gui_->CreateScopedStyleColor(
        ImGuiCol_Button, mode_ == Mode::kTiles ? kActiveButtonColor : kInactiveButtonColor);
    if (gui_->Button("Tiles")) {
      mode_ = Mode::kTiles;
      blueprint_panel_->ClearSelection();
    }
  }

  gui_->Separator();

  if (mode_ == Mode::kBlueprints) return blueprint_panel_->Render();
  return tile_panel_->Render(tile_render_width, tile_render_height);
}

const Blueprint* PalettePanel::GetSelectedBlueprint() const {
  if (mode_ != Mode::kBlueprints) return nullptr;
  return blueprint_panel_->GetSelectedBlueprint();
}

bool PalettePanel::GetSnapToGrid() const { return blueprint_panel_->GetSnapToGrid(); }

bool PalettePanel::GetShowEntityBorders() const {
  return blueprint_panel_->GetShowEntityBorders();
}

const Tile* PalettePanel::GetSelectedTile() const {
  if (mode_ != Mode::kTiles) return nullptr;
  return tile_panel_->GetSelectedTile();
}

const Tileset* PalettePanel::GetSelectedTileset() const {
  if (mode_ != Mode::kTiles) return nullptr;
  return tile_panel_->GetSelectedTileset();
}

bool PalettePanel::GetShowTileFrame() const { return tile_panel_->GetShowTileFrame(); }

bool PalettePanel::GetShowTileCollision() const { return tile_panel_->GetShowTileCollision(); }

}  // namespace zebes
