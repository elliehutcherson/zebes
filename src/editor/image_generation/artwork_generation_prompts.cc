#include "editor/image_generation/artwork_generation_prompts.h"

#include <string>
#include <string_view>

namespace zebes {

const char* ArtworkGenerationStylePresetLabel(ArtworkGenerationStylePreset preset) {
  switch (preset) {
    case ArtworkGenerationStylePreset::kCustom:
      return "Custom";
    case ArtworkGenerationStylePreset::kRetroExploration:
      return "Retro exploration";
    case ArtworkGenerationStylePreset::kModernPixelArt:
      return "Modern pixel art";
    case ArtworkGenerationStylePreset::kHandPaintedPlatformer:
      return "Hand-painted platformer";
    case ArtworkGenerationStylePreset::kDarkBiomechanical:
      return "Dark biomechanical";
    case ArtworkGenerationStylePreset::kStylizedFantasy:
      return "Stylized fantasy";
    case ArtworkGenerationStylePreset::kCleanCartoon:
      return "Clean cartoon";
  }
  return "Invalid";
}

const char* ArtworkGenerationStylePresetGuidance(ArtworkGenerationStylePreset preset) {
  switch (preset) {
    case ArtworkGenerationStylePreset::kCustom:
      return "";
    case ArtworkGenerationStylePreset::kRetroExploration:
      return kRetroExplorationStyleGuidance;
    case ArtworkGenerationStylePreset::kModernPixelArt:
      return kModernPixelArtStyleGuidance;
    case ArtworkGenerationStylePreset::kHandPaintedPlatformer:
      return kHandPaintedPlatformerStyleGuidance;
    case ArtworkGenerationStylePreset::kDarkBiomechanical:
      return kDarkBiomechanicalStyleGuidance;
    case ArtworkGenerationStylePreset::kStylizedFantasy:
      return kStylizedFantasyStyleGuidance;
    case ArtworkGenerationStylePreset::kCleanCartoon:
      return kCleanCartoonStyleGuidance;
  }
  return "";
}

std::string AppendArtworkGenerationStyle(std::string instructions, std::string_view guidance) {
  if (guidance.empty()) return instructions;
  if (!instructions.empty()) instructions.append("\n\n");
  instructions.append("Art direction:\n");
  instructions.append(guidance);
  return instructions;
}

}  // namespace zebes
