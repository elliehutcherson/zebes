#include "editor/terrain_editor/terrain_controls_panel.h"

#include <algorithm>
#include <array>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "editor/imgui_scoped.h"

namespace zebes {
namespace {

constexpr float kControlWidth = 170.0f;
constexpr float kPathWidth = 200.0f;
constexpr char kManifestDialogKey[] = "TerrainManifestDlg";

// Sections start open: a panel of collapsed headers hides the fact that there
// is anything to tune at all.
constexpr ImGuiTreeNodeFlags kSectionFlags = ImGuiTreeNodeFlags_DefaultOpen;

ImVec4 ToColor(uint32_t rgb) {
  return ImVec4(static_cast<float>((rgb >> 16) & 0xff) / 255.0f,
                static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
                static_cast<float>(rgb & 0xff) / 255.0f, 1.0f);
}

uint32_t FromColor(const ImVec4& color) {
  const uint32_t r = static_cast<uint32_t>(color.x * 255.0f + 0.5f);
  const uint32_t g = static_cast<uint32_t>(color.y * 255.0f + 0.5f);
  const uint32_t b = static_cast<uint32_t>(color.z * 255.0f + 0.5f);
  return (r << 16) | (g << 8) | b;
}

constexpr std::array<std::pair<TerrainAccentMode, const char*>, 3> kAccentModes = {{
    {TerrainAccentMode::kMaterial, "Material"},
    {TerrainAccentMode::kAccent, "Accent"},
    {TerrainAccentMode::kGradient, "Gradient"},
}};

}  // namespace

absl::StatusOr<std::unique_ptr<TerrainControlsPanel>> TerrainControlsPanel::Create(
    GuiInterface* gui) {
  if (gui == nullptr) return absl::InvalidArgumentError("TerrainControlsPanel requires a GUI");
  return absl::WrapUnique(new TerrainControlsPanel(gui));
}

void TerrainControlsPanel::Explain(const char* description) {
  if (!gui_->IsItemHovered()) return;
  gui_->SetTooltip("%s", description);
}

bool TerrainControlsPanel::RenderThemeSection(TerrainEditorModel& model) {
  if (!gui_->CollapsingHeader("Material##TerrainGen", kSectionFlags)) return false;

  TerrainGenConfig& config = model.config();
  bool changed = false;
  bool manual_changed = false;
  {
    const char* preview =
        model.selected_preset().has_value() ? model.selected_preset()->c_str() : "Custom";
    ScopedCombo combo = gui_->CreateScopedCombo("Preset##TerrainGen", preview);
    if (combo.IsActive()) {
      for (const TerrainPreset& preset : BuiltInTerrainPresets()) {
        const bool selected =
            model.selected_preset().has_value() && preset.name == *model.selected_preset();
        if (!gui_->Selectable(preset.name.c_str(), selected)) continue;
        model.ApplyPreset(preset);
        changed = true;
      }
    }
  }
  Explain(
      "A complete visual starting point, including pixel profile and texture styles. Quality and "
      "seed stay unchanged.");

  ImVec4 surface = ToColor(config.material.surface);
  if (gui_->ColorEdit3("Surface##TerrainGen", &surface.x)) {
    config.material.surface = FromColor(surface);
    manual_changed = true;
  }
  Explain("The band along the outside: grass on soil, snow on rock.");

  ImVec4 substrate = ToColor(config.material.substrate);
  if (gui_->ColorEdit3("Interior##TerrainGen", &substrate.x)) {
    config.material.substrate = FromColor(substrate);
    manual_changed = true;
  }
  Explain("The bulk of the material. Outline, shadow and flecks are all derived from it.");

  ImVec4 outline = ToColor(config.material.outline);
  if (gui_->ColorEdit3("Outline colour##TerrainGen", &outline.x)) {
    config.material.outline = FromColor(outline);
    manual_changed = true;
  }
  Explain("A separately chosen warm outline keeps cheerful materials from being edged in black.");

  ImVec4 accent = ToColor(config.material.accent_primary);
  if (gui_->ColorEdit3("Accent##TerrainGen", &accent.x)) {
    config.material.accent_primary = FromColor(accent);
    manual_changed = true;
  }
  Explain("Primary colour for flowers, flakes, crystals and other readable details.");

  ImVec4 accent_secondary = ToColor(config.material.accent_secondary);
  if (gui_->ColorEdit3("Accent 2##TerrainGen", &accent_secondary.x)) {
    config.material.accent_secondary = FromColor(accent_secondary);
    manual_changed = true;
  }
  Explain("Secondary petal, sparkle and crystal colour.");

  gui_->SetNextItemWidth(kControlWidth);
  manual_changed |=
      gui_->SliderFloat("Hue drift##TerrainGen", &config.material.hue_shift, -0.15f, 0.15f);
  Explain(
      "How far colour shifts as it darkens. Zero gives a flat luminance ramp, which reads as "
      "dead; a little drift is what makes it look drawn.");

  gui_->SetNextItemWidth(kControlWidth);
  manual_changed |=
      gui_->SliderFloat("Contrast##TerrainGen", &config.material.contrast, 0.4f, 1.6f);
  Explain("Distance between highlights and shadows. Chunky pixel art usually wants more.");

  int seed = static_cast<int>(config.seed);
  gui_->SetNextItemWidth(kControlWidth);
  if (gui_->InputInt("Seed##TerrainGen", &seed)) {
    config.seed = static_cast<uint64_t>(std::max(0, seed));
    changed = true;
  }
  Explain("Changes the random pattern without changing any of its qualities.");

  if (manual_changed) model.MarkConfigCustom();
  return changed || manual_changed;
}

bool TerrainControlsPanel::RenderSurfaceSection(TerrainGenConfig& config) {
  if (!gui_->CollapsingHeader("Surface##TerrainGen", kSectionFlags)) return false;

  bool changed = false;
  static constexpr std::array<std::pair<TerrainSurfaceStyle, const char*>, 4> kSurfaceStyles = {{
      {TerrainSurfaceStyle::kSmooth, "Smooth"},
      {TerrainSurfaceStyle::kTufted, "Tufted"},
      {TerrainSurfaceStyle::kScalloped, "Scalloped"},
      {TerrainSurfaceStyle::kMossy, "Mossy"},
  }};
  const char* surface_style_name = "Unknown";
  for (const auto& [value, name] : kSurfaceStyles) {
    if (value == config.material.surface_style) surface_style_name = name;
  }
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Texture##TerrainSurface", surface_style_name);
    if (combo.IsActive()) {
      for (const auto& [value, name] : kSurfaceStyles) {
        if (!gui_->Selectable(name, value == config.material.surface_style)) continue;
        config.material.surface_style = value;
        changed = true;
      }
    }
  }
  Explain("How the surface colour is clustered without changing the collision silhouette.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Depth##TerrainGen", &config.grass_band, 1.0f, 20.0f);
  Explain("How far the surface band reaches into the material, in profile reference pixels.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Waviness##TerrainGen", &config.ruffle_amplitude, 0.0f, 8.0f);
  Explain("How much the inner edge of the band wanders. Zero gives a band of even depth.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Wave size##TerrainGen", &config.ruffle_density, 0.5f, 6.0f);
  Explain("Roughly how many waves span one tile. Higher is busier.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Wave shape##TerrainGen", &config.ruffle_sharpness, 0.2f, 3.0f);
  Explain("Below 1 rounds the waves into blobs; above 1 makes them spiky.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Wave detail##TerrainGen", &config.ruffle_octaves, 1, 3);
  Explain("Adds finer waves on top of the coarse ones.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderFloat("Underside##TerrainGen", &config.grass_bottom_bias, 0.0f, 1.0f);
  Explain(
      "How thick the band is on downward-facing surfaces. Low values leave overhangs nearly "
      "bare, which is what makes ground look lit from above.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Outline##TerrainGen", &config.outline_width, 0, 3);
  Explain("Width of the dark line around the silhouette, in profile reference pixels.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Highlight##TerrainGen", &config.grass_hi_depth, 0, 8);
  Explain("Depth of the lit edge just inside the outline.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Shade##TerrainGen", &config.grass_shade_depth, 0, 8);
  Explain("Depth of the darker band where the surface meets the interior.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Contact shadow##TerrainGen", &config.contact_depth, 0, 8);
  Explain("How far the surface casts a shadow down into the interior.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |=
      gui_->SliderFloat("Texture size##TerrainGen", &config.surface_texture_size, 1.0f, 12.0f);
  Explain("Feature size in the selected pixel profile's reference pixels.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |=
      gui_->SliderFloat("Texture amount##TerrainGen", &config.surface_texture_amount, 0.0f, 1.0f);
  Explain("How strongly tufts, scallops or moss break up the flat surface colour.");

  return changed;
}

bool TerrainControlsPanel::RenderAccentMode(const char* label, TerrainAccentMode& mode) {
  const char* mode_name = "Unknown";
  for (const auto& [value, name] : kAccentModes) {
    if (value == mode) mode_name = name;
  }

  bool changed = false;
  {
    ScopedCombo combo = gui_->CreateScopedCombo(label, mode_name);
    if (combo.IsActive()) {
      for (const auto& [value, name] : kAccentModes) {
        if (!gui_->Selectable(name, value == mode)) continue;
        mode = value;
        changed = true;
      }
    }
  }
  Explain(
      "Material tints these marks like the substrate they sit in. Accent uses the two accent "
      "colours flat, and Gradient sweeps between them across each mark.");
  return changed;
}

bool TerrainControlsPanel::RenderInteriorSection(TerrainGenConfig& config) {
  if (!gui_->CollapsingHeader("Interior##TerrainGen", kSectionFlags)) return false;

  bool changed = false;
  static constexpr std::array<std::pair<TerrainInteriorStyle, const char*>, 4> kInteriorStyles = {{
      {TerrainInteriorStyle::kFlat, "Flat"},
      {TerrainInteriorStyle::kMottle, "Mottle"},
      {TerrainInteriorStyle::kSoilClods, "Soil clods"},
      {TerrainInteriorStyle::kCobbles, "Cobbles"},
  }};
  const char* interior_style_name = "Unknown";
  for (const auto& [value, name] : kInteriorStyles) {
    if (value == config.interior.base.style) interior_style_name = name;
  }
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Texture##TerrainInterior", interior_style_name);
    if (combo.IsActive()) {
      for (const auto& [value, name] : kInteriorStyles) {
        if (!gui_->Selectable(name, value == config.interior.base.style)) continue;
        config.interior.base.style = value;
        changed = true;
      }
    }
  }
  Explain("Broad mottle, outlined soil cells, or stone-like cells in the material bulk.");

  if (config.interior.base.style == TerrainInteriorStyle::kMottle) {
    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderFloat("Mottle size##TerrainGen", &config.interior.base.mottle_density,
                                 0.5f, 8.0f);
    Explain("How many blotches span a tile. Higher is finer.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderFloat("Mottle amount##TerrainGen", &config.interior.base.mottle_coverage,
                                 0.0f, 1.0f);
    Explain("How much of the interior the darker blotches cover. Zero leaves it flat.");
  } else if (config.interior.base.style == TerrainInteriorStyle::kSoilClods ||
             config.interior.base.style == TerrainInteriorStyle::kCobbles) {
    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderFloat("Feature size##TerrainGen", &config.interior.base.feature_size,
                                 2.0f, 16.0f);
    Explain("Reference-pixel size of a soil clod or cobble.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderFloat("Relief##TerrainGen", &config.interior.base.relief, 0.0f, 1.0f);
    Explain("Strength of the seams and small highlights that give material cells volume.");
  }

  static constexpr std::array<std::pair<TerrainSubstratePattern, const char*>, 6> kPatterns = {{
      {TerrainSubstratePattern::kNone, "None"},
      {TerrainSubstratePattern::kPebbles, "Pebbles"},
      {TerrainSubstratePattern::kFlecks, "Flecks"},
      {TerrainSubstratePattern::kCrosses, "Crosses"},
      {TerrainSubstratePattern::kDiamonds, "Diamonds"},
      {TerrainSubstratePattern::kMixedEarth, "Mixed earth"},
  }};
  const char* pattern_name = "Unknown";
  for (const auto& [value, name] : kPatterns) {
    if (value == config.interior.pattern.family) pattern_name = name;
  }
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Pattern##TerrainInterior", pattern_name);
    if (combo.IsActive()) {
      for (const auto& [value, name] : kPatterns) {
        if (!gui_->Selectable(name, value == config.interior.pattern.family)) continue;
        config.interior.pattern.family = value;
        changed = true;
      }
    }
  }
  Explain("Small material marks embedded in the substrate, independently of flowers or props.");

  if (config.interior.pattern.family != TerrainSubstratePattern::kNone) {
    gui_->SetNextItemWidth(kControlWidth);
    changed |=
        gui_->SliderInt("Pattern amount##TerrainGen", &config.interior.pattern.density, 0, 12);
    Explain("Substrate motifs scattered per tile.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |=
        gui_->SliderInt("Pattern spacing##TerrainGen", &config.interior.pattern.spacing, 1, 24);
    Explain("Smallest gap between substrate motifs. Grows with the pattern size.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderInt("Pattern size##TerrainGen", &config.interior.pattern.scale, 1, 4);
    Explain("Magnifies each mark. The art stays crisp; spacing grows to match.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderInt("Pattern margin##TerrainGen", &config.interior.pattern.margin, 0, 4);
    Explain("Clearance a mark needs from the surface band before it will be placed at all.");

    changed |= RenderAccentMode("Pattern accent##TerrainGen", config.interior.pattern.accent_mode);

    // Contrast only shapes the substrate ramp, which the accent modes bypass.
    // A live slider that does nothing is worse than a disabled one.
    {
      ScopedDisabled disabled = gui_->CreateScopedDisabled(config.interior.pattern.accent_mode !=
                                                           TerrainAccentMode::kMaterial);
      gui_->SetNextItemWidth(kControlWidth);
      changed |= gui_->SliderFloat("Pattern contrast##TerrainGen",
                                   &config.interior.pattern.contrast, 0.0f, 1.0f);
    }
    Explain("How strongly the marks separate from the interior colour. Material accent only.");
  }

  static constexpr std::array<std::pair<TerrainDetailSet, const char*>, 5> kDetailSets = {{
      {TerrainDetailSet::kNone, "None"},
      {TerrainDetailSet::kMeadow, "Meadow"},
      {TerrainDetailSet::kForestFloor, "Forest floor"},
      {TerrainDetailSet::kSnow, "Snow"},
      {TerrainDetailSet::kCrystals, "Crystals"},
  }};
  const char* detail_set_name = "Unknown";
  for (const auto& [value, name] : kDetailSets) {
    if (value == config.interior.details.family) detail_set_name = name;
  }
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Details##TerrainGen", detail_set_name);
    if (combo.IsActive()) {
      for (const auto& [value, name] : kDetailSets) {
        if (!gui_->Selectable(name, value == config.interior.details.family)) continue;
        config.interior.details.family = value;
        // Changing the family is choosing a different material, so the accent
        // mode follows it to that family's usual reading. Picking Crystals and
        // getting dirt-tinted gems would be a surprise; the mode stays
        // overridable afterwards.
        config.interior.details.accent_mode = DefaultAccentModeFor(value);
        changed = true;
      }
    }
  }
  Explain("Readable objects such as flowers, roots, flakes or crystals.");

  if (config.interior.details.family != TerrainDetailSet::kNone) {
    gui_->SetNextItemWidth(kControlWidth);
    changed |=
        gui_->SliderInt("Detail amount##TerrainGen", &config.interior.details.density, 0, 12);
    Explain("Semantic objects scattered per tile.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |=
        gui_->SliderInt("Detail spacing##TerrainGen", &config.interior.details.spacing, 1, 24);
    Explain("Smallest gap between semantic details. Grows with the detail size.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderInt("Detail size##TerrainGen", &config.interior.details.scale, 1, 4);
    Explain("Magnifies each object. The art stays crisp; spacing grows to match.");

    gui_->SetNextItemWidth(kControlWidth);
    changed |= gui_->SliderInt("Detail margin##TerrainGen", &config.interior.details.margin, 0, 4);
    Explain("Clearance an object needs from the surface band before it will be placed at all.");

    changed |= RenderAccentMode("Detail accent##TerrainGen", config.interior.details.accent_mode);
  }

  return changed;
}

bool TerrainControlsPanel::RenderPatternSection(TerrainEditorModel& model) {
  if (!gui_->CollapsingHeader("Pattern##TerrainGen", kSectionFlags)) return false;

  TerrainGenConfig& config = model.config();
  bool changed = false;

  static constexpr std::array<std::pair<TerrainPixelProfile, const char*>, 3> kProfiles = {{
      {TerrainPixelProfile::kChunky16, "Chunky 16"},
      {TerrainPixelProfile::kBalanced32, "Balanced 32"},
      {TerrainPixelProfile::kDetailed64, "Detailed 64"},
  }};
  const char* profile_name = "Unknown";
  for (const auto& [value, name] : kProfiles) {
    if (value == config.pixel_profile) profile_name = name;
  }
  {
    ScopedCombo combo = gui_->CreateScopedCombo("Pixel style##TerrainGen", profile_name);
    if (combo.IsActive()) {
      for (const auto& [value, name] : kProfiles) {
        if (!gui_->Selectable(name, value == config.pixel_profile)) continue;
        config.pixel_profile = value;
        changed = true;
      }
    }
  }
  Explain("Controls palette compactness, feature quantisation and the reference design scale.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Tile size##TerrainGen", &config.tile_size, 8, 64);
  Explain("Edge length of one tile in pixels. Levels using this terrain draw at this size.");

  gui_->SetNextItemWidth(kControlWidth);
  changed |= gui_->SliderInt("Repeat over##TerrainGen", &config.variant_period, 1, 3);
  Explain(
      "How many tiles the surface pattern takes to repeat. At 1 every tile is drawn from the "
      "same one-tile pattern, so a long run shows a rhythm at the tile size. Raising it costs "
      "tiles: the count is the square of this number.");

  gui_->TextDisabled("%s", absl::StrFormat("%d tiles", model.TileCount()).c_str());

  return changed;
}

bool TerrainControlsPanel::RenderManifestSection(TerrainEditorModel& model) {
  gui_->Text("Manifest");
  gui_->TextWrapped(
      "Import artwork you composed with the compose_blob47 tool. Import the atlas as a texture "
      "first, then choose it in the Output column.");

  bool changed = false;
  gui_->SetNextItemWidth(kPathWidth);
  changed |= gui_->InputText("Path##TerrainManifest", &model.manifest_path());
  if (gui_->Button("Browse##TerrainManifest")) {
    gui_->OpenFileDialog(kManifestDialogKey, "Choose Terrain Manifest", ".json", ".");
  }

  // Drawn every frame because the dialog outlives the frame that opened it.
  if (std::optional<std::string> chosen = gui_->DisplayFileDialog(kManifestDialogKey);
      chosen.has_value()) {
    model.manifest_path() = *std::move(chosen);
    changed = true;
  }
  return changed;
}

bool TerrainControlsPanel::Render(TerrainEditorModel& model) {
  if (model.source() == TerrainEditorModel::Source::kImportManifest) {
    return RenderManifestSection(model);
  }

  // Every section reports independently so that collapsing one does not hide
  // whether another moved.
  bool changed = RenderThemeSection(model);
  const bool surface_changed = RenderSurfaceSection(model.config());
  const bool interior_changed = RenderInteriorSection(model.config());
  const bool pattern_changed = RenderPatternSection(model);
  if (surface_changed || interior_changed || pattern_changed) model.MarkConfigCustom();
  changed |= surface_changed;
  changed |= interior_changed;
  changed |= pattern_changed;
  return changed;
}

}  // namespace zebes
