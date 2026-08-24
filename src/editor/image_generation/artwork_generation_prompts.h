#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace zebes {

inline constexpr char kDefaultPropGenerationInstructions[] =
    "Create one isolated game prop centered on a square canvas. Show the complete subject "
    "without cropping. The subject must be free of any depicted environment: use a transparent "
    "background when supported, otherwise use one flat solid-color background that clearly "
    "contrasts with the subject. Do not add scenery, a floor or ground plane, cast shadows, "
    "text, a border, a frame, or additional objects.";

inline constexpr char kDefaultParallaxGenerationInstructions[] =
    "Create one camera-relative parallax background layer for a side-view exploration game. "
    "Fill the entire canvas with a coherent environment composition. Keep strong silhouettes "
    "and broad value groups so gameplay remains readable. Do not add text, borders, UI, or a "
    "picture frame.";

inline constexpr char kRetroExplorationStyleGuidance[] =
    "16-bit science-fiction exploration game art. Use a limited palette, strong silhouette, "
    "crisp pixel clusters, a subtle dark outline, and a readable side view.";
inline constexpr char kModernPixelArtStyleGuidance[] =
    "Modern pixel-art game asset. Use deliberate pixel clusters, selective anti-aliasing, "
    "compact shading, and a strong silhouette that remains readable at gameplay scale.";
inline constexpr char kHandPaintedPlatformerStyleGuidance[] =
    "Hand-painted 2D platformer game art. Use simplified shapes, soft painted shading, "
    "controlled texture, and a clear three-quarter silhouette.";
inline constexpr char kDarkBiomechanicalStyleGuidance[] =
    "Dark biomechanical science-fiction game art. Blend organic and mechanical forms, use a "
    "muted alien palette with restrained highlights, high-contrast shapes, and ominous surface "
    "detail.";
inline constexpr char kStylizedFantasyStyleGuidance[] =
    "Stylized fantasy adventure game art. Use appealing exaggerated proportions, painterly "
    "materials, a cohesive vivid palette, and an iconic three-quarter silhouette.";
inline constexpr char kCleanCartoonStyleGuidance[] =
    "Clean cartoon game art. Use bold simple shapes, flat colors, restrained two-tone shading, "
    "a clear dark outline, and instantly readable features.";

inline constexpr char kOpaquePlateGenerationGuidance[] =
    "Layer role: opaque plate; fill every edge of the canvas.";
inline constexpr char kTransparentOverlayGenerationGuidance[] =
    "Layer role: transparent overlay; return a transparent background.";
inline constexpr char kMatteOverlayGenerationGuidance[] =
    "Layer role: transparent overlay; place the foreground shapes over one flat, strongly "
    "contrasting matte color for deterministic removal.";
inline constexpr char kSeamlessHorizontalGenerationGuidance[] =
    "Horizontal repetition: make the left and right edges seamlessly tile.";
inline constexpr char kNonRepeatingHorizontalGenerationGuidance[] =
    "Horizontal repetition is not required; avoid an obvious repeated motif at both edges.";
inline constexpr char kDefaultBackgroundPaletteGuidance[] =
    "Palette and style: use the editor's configured background-art style.";

enum class ArtworkGenerationStylePreset : uint8_t {
  kCustom = 0,
  kRetroExploration = 1,
  kModernPixelArt = 2,
  kHandPaintedPlatformer = 3,
  kDarkBiomechanical = 4,
  kStylizedFantasy = 5,
  kCleanCartoon = 6,
};

inline constexpr std::array<ArtworkGenerationStylePreset, 7> kArtworkGenerationStylePresets = {
    ArtworkGenerationStylePreset::kCustom,
    ArtworkGenerationStylePreset::kRetroExploration,
    ArtworkGenerationStylePreset::kModernPixelArt,
    ArtworkGenerationStylePreset::kHandPaintedPlatformer,
    ArtworkGenerationStylePreset::kDarkBiomechanical,
    ArtworkGenerationStylePreset::kStylizedFantasy,
    ArtworkGenerationStylePreset::kCleanCartoon,
};

const char* ArtworkGenerationStylePresetLabel(ArtworkGenerationStylePreset preset);
const char* ArtworkGenerationStylePresetGuidance(ArtworkGenerationStylePreset preset);

// Adds the optional user-editable style policy to otherwise complete
// domain-specific generation instructions.
std::string AppendArtworkGenerationStyle(std::string instructions, std::string_view guidance);

}  // namespace zebes
