#include "resources/asset_references.h"

#include "absl/strings/str_cat.h"

namespace zebes {
namespace {

// A reference is a non-empty ID that matches. Empty is not a miss, it is the
// absence of a reference: an unbound sprite or an unfinished parallax layer
// names nothing and must not be reported as pointing at whatever is being
// deleted.
bool Names(std::string_view field, std::string_view id) { return !field.empty() && field == id; }

void Add(std::vector<AssetReference>& out, AssetKind kind, std::string_view id,
         std::string_view display_name, std::string field) {
  out.push_back(AssetReference{
      .kind = kind,
      .id = std::string(id),
      .display_name = std::string(display_name),
      .field = std::move(field),
  });
}

// Walks a reusable theme for elements naming `texture_id`, recording both
// owning layer and element so the user can find the one that matters.
void AddParallaxReferences(std::vector<AssetReference>& out, const ParallaxTheme& theme,
                           std::string_view texture_id) {
  for (const ParallaxLayer& layer : theme.layers) {
    for (const ParallaxElement& element : layer.elements) {
      if (!Names(element.texture_id, texture_id)) continue;
      Add(out, AssetKind::kParallaxTheme, theme.id, theme.name,
          absl::StrCat("layer '", layer.name, "', element '", element.name, "'"));
    }
  }
}

}  // namespace

std::string_view AssetKindName(AssetKind kind) {
  switch (kind) {
    case AssetKind::kTexture:
      return "Texture";
    case AssetKind::kTileset:
      return "Tileset";
    case AssetKind::kSprite:
      return "Sprite";
    case AssetKind::kCollider:
      return "Collider";
    case AssetKind::kBlueprint:
      return "Blueprint";
    case AssetKind::kLevel:
      return "Level";
    case AssetKind::kParallaxTheme:
      return "Parallax theme";
    case AssetKind::kTerrainRecipe:
      return "Terrain recipe";
    case AssetKind::kSourceArtwork:
      return "Source artwork";
    case AssetKind::kPropRecipe:
      return "Prop recipe";
    case AssetKind::kParallaxArtworkRecipe:
      return "Parallax artwork recipe";
  }
  return "Unknown";
}

std::vector<AssetReference> FindTextureReferrers(const AssetCatalog& catalog,
                                                 std::string_view texture_id) {
  std::vector<AssetReference> referrers;
  if (texture_id.empty()) return referrers;

  for (const Tileset& tileset : catalog.tilesets) {
    if (Names(tileset.texture_id, texture_id)) {
      Add(referrers, AssetKind::kTileset, tileset.id, tileset.name, "texture_id");
    }
  }
  for (const Sprite& sprite : catalog.sprites) {
    if (Names(sprite.texture_id, texture_id)) {
      Add(referrers, AssetKind::kSprite, sprite.id, sprite.name, "texture_id");
    }
  }
  for (const ParallaxTheme& theme : catalog.parallax_themes) {
    AddParallaxReferences(referrers, theme, texture_id);
  }
  for (const TerrainRecipe& recipe : catalog.recipes) {
    if (Names(recipe.texture_id, texture_id)) {
      Add(referrers, AssetKind::kTerrainRecipe, recipe.id, recipe.name, "texture_id");
    }
  }
  for (const PropRecipe& recipe : catalog.prop_recipes) {
    if (Names(recipe.texture_id, texture_id)) {
      Add(referrers, AssetKind::kPropRecipe, recipe.id, recipe.name, "texture_id");
    }
  }
  for (const ParallaxArtworkRecipe& recipe : catalog.parallax_artwork_recipes) {
    if (Names(recipe.texture_id, texture_id)) {
      Add(referrers, AssetKind::kParallaxArtworkRecipe, recipe.id, recipe.name, "texture_id");
    }
  }
  return referrers;
}

std::vector<AssetReference> FindParallaxThemeReferrers(const AssetCatalog& catalog,
                                                       std::string_view theme_id) {
  std::vector<AssetReference> referrers;
  if (theme_id.empty()) return referrers;
  for (const Level& level : catalog.levels) {
    for (const ParallaxZone& zone : level.zones) {
      if (!Names(zone.theme_id, theme_id)) continue;
      Add(referrers, AssetKind::kLevel, level.id, level.name,
          absl::StrCat("zone '", zone.name, "'"));
    }
  }
  return referrers;
}

std::vector<AssetReference> FindTilesetReferrers(const AssetCatalog& catalog,
                                                 std::string_view tileset_id) {
  std::vector<AssetReference> referrers;
  if (tileset_id.empty()) return referrers;

  for (const Level& level : catalog.levels) {
    if (Names(level.tileset_id, tileset_id)) {
      Add(referrers, AssetKind::kLevel, level.id, level.name, "tileset_id");
    }
  }
  for (const TerrainRecipe& recipe : catalog.recipes) {
    if (Names(recipe.tileset_id, tileset_id)) {
      Add(referrers, AssetKind::kTerrainRecipe, recipe.id, recipe.name, "tileset_id");
    }
  }
  return referrers;
}

std::vector<AssetReference> FindSpriteReferrers(const AssetCatalog& catalog,
                                                std::string_view sprite_id) {
  std::vector<AssetReference> referrers;
  if (sprite_id.empty()) return referrers;

  for (const Blueprint& blueprint : catalog.blueprints) {
    for (size_t i = 0; i < blueprint.states.size(); ++i) {
      if (!Names(blueprint.states[i].sprite_id, sprite_id)) continue;
      Add(referrers, AssetKind::kBlueprint, blueprint.id, blueprint.name,
          absl::StrCat("state '", blueprint.states[i].name, "'"));
    }
  }
  // An entity carries its own asset IDs rather than resolving them through its
  // blueprint every frame, so a level can name a sprite no blueprint does.
  for (const Level& level : catalog.levels) {
    for (const WorldLayer& layer : level.layers) {
      for (const auto& [entity_id, entity] : layer.entities) {
        if (!Names(entity.sprite_id, sprite_id)) continue;
        Add(referrers, AssetKind::kLevel, level.id, level.name,
            absl::StrCat("layer '", layer.name, "', entity ", entity_id));
      }
    }
  }
  for (const PropRecipe& recipe : catalog.prop_recipes) {
    if (Names(recipe.sprite_id, sprite_id)) {
      Add(referrers, AssetKind::kPropRecipe, recipe.id, recipe.name, "sprite_id");
    }
  }
  return referrers;
}

std::vector<AssetReference> FindColliderReferrers(const AssetCatalog& catalog,
                                                  std::string_view collider_id) {
  std::vector<AssetReference> referrers;
  if (collider_id.empty()) return referrers;

  for (const Blueprint& blueprint : catalog.blueprints) {
    for (size_t i = 0; i < blueprint.states.size(); ++i) {
      if (!Names(blueprint.states[i].collider_id, collider_id)) continue;
      Add(referrers, AssetKind::kBlueprint, blueprint.id, blueprint.name,
          absl::StrCat("state '", blueprint.states[i].name, "'"));
    }
  }
  for (const Level& level : catalog.levels) {
    for (const WorldLayer& layer : level.layers) {
      for (const auto& [entity_id, entity] : layer.entities) {
        if (!Names(entity.collider_id, collider_id)) continue;
        Add(referrers, AssetKind::kLevel, level.id, level.name,
            absl::StrCat("layer '", layer.name, "', entity ", entity_id));
      }
    }
  }
  return referrers;
}

std::vector<AssetReference> FindBlueprintReferrers(const AssetCatalog& catalog,
                                                   std::string_view blueprint_id) {
  std::vector<AssetReference> referrers;
  if (blueprint_id.empty()) return referrers;

  for (const Level& level : catalog.levels) {
    for (const WorldLayer& layer : level.layers) {
      for (const auto& [entity_id, entity] : layer.entities) {
        if (!Names(entity.blueprint_id, blueprint_id)) continue;
        Add(referrers, AssetKind::kLevel, level.id, level.name,
            absl::StrCat("layer '", layer.name, "', entity ", entity_id));
      }
    }
  }
  for (const PropRecipe& recipe : catalog.prop_recipes) {
    if (Names(recipe.blueprint_id, blueprint_id)) {
      Add(referrers, AssetKind::kPropRecipe, recipe.id, recipe.name, "blueprint_id");
    }
  }
  return referrers;
}

std::vector<AssetReference> FindSourceArtworkReferrers(const AssetCatalog& catalog,
                                                       std::string_view source_artwork_id) {
  std::vector<AssetReference> referrers;
  if (source_artwork_id.empty()) return referrers;
  for (const PropRecipe& recipe : catalog.prop_recipes) {
    if (Names(recipe.source_artwork_id, source_artwork_id)) {
      Add(referrers, AssetKind::kPropRecipe, recipe.id, recipe.name, "source_artwork_id");
    }
  }
  for (const ParallaxArtworkRecipe& recipe : catalog.parallax_artwork_recipes) {
    if (Names(recipe.source_artwork_id, source_artwork_id)) {
      Add(referrers, AssetKind::kParallaxArtworkRecipe, recipe.id, recipe.name,
          "source_artwork_id");
    }
  }
  return referrers;
}

std::vector<AssetReference> FindTerrainRecipeReferrers(const AssetCatalog& catalog,
                                                       std::string_view terrain_recipe_id) {
  std::vector<AssetReference> referrers;
  if (terrain_recipe_id.empty()) return referrers;
  for (const PropRecipe& recipe : catalog.prop_recipes) {
    if (recipe.terrain_recipe_id.has_value() &&
        Names(*recipe.terrain_recipe_id, terrain_recipe_id)) {
      Add(referrers, AssetKind::kPropRecipe, recipe.id, recipe.name, "terrain_recipe_id");
    }
  }
  for (const ParallaxArtworkRecipe& recipe : catalog.parallax_artwork_recipes) {
    if (recipe.terrain_recipe_id.has_value() &&
        Names(*recipe.terrain_recipe_id, terrain_recipe_id)) {
      Add(referrers, AssetKind::kParallaxArtworkRecipe, recipe.id, recipe.name,
          "terrain_recipe_id");
    }
  }
  return referrers;
}

std::vector<AssetReference> FindTileReferrers(const AssetCatalog& catalog,
                                              std::string_view tileset_id, int tile_id) {
  std::vector<AssetReference> referrers;
  // Zero is the empty cell rather than a tile, so nothing can reference it.
  if (tileset_id.empty() || tile_id <= 0) return referrers;

  for (const Level& level : catalog.levels) {
    if (!Names(level.tileset_id, tileset_id)) continue;

    int painted = 0;
    for (const WorldLayer& layer : level.layers) {
      for (const auto& entry : layer.tile_chunks) {
        for (const int painted_id : entry.second.tiles) {
          if (painted_id == tile_id) ++painted;
        }
      }
    }
    if (painted == 0) continue;
    // The count, because "this level uses it" leaves the user hunting a whole
    // world for cells they would then have to repaint.
    Add(referrers, AssetKind::kLevel, level.id, level.name,
        absl::StrCat(painted, painted == 1 ? " painted cell" : " painted cells"));
  }
  return referrers;
}

std::string DescribeBlockedDeletion(std::string_view subject,
                                    const std::vector<AssetReference>& referrers) {
  std::string message =
      absl::StrCat("Cannot delete ", subject, ". ", referrers.size(),
                   referrers.size() == 1 ? " thing references it:" : " things reference it:");
  for (const AssetReference& referrer : referrers) {
    absl::StrAppend(&message, "\n  ", AssetKindName(referrer.kind), " '", referrer.display_name,
                    "' (", referrer.field, ")");
  }
  return message;
}

}  // namespace zebes
