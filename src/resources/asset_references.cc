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

// Walks a level's themes for parallax layers naming `texture_id`, recording the
// theme and layer so the user can find the one that matters.
void AddParallaxReferences(std::vector<AssetReference>& out, const Level& level,
                           std::string_view texture_id) {
  for (const auto& [theme_id, theme] : level.themes) {
    for (const ParallaxLayer& layer : theme.layers) {
      if (!Names(layer.texture_id, texture_id)) continue;
      Add(out, AssetKind::kLevel, level.id, level.name,
          absl::StrCat("theme '", theme.name, "', layer '", layer.name, "'"));
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
    case AssetKind::kTerrainRecipe:
      return "Terrain recipe";
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
  for (const Level& level : catalog.levels) {
    AddParallaxReferences(referrers, level, texture_id);
  }
  for (const TerrainRecipe& recipe : catalog.recipes) {
    if (Names(recipe.texture_id, texture_id)) {
      Add(referrers, AssetKind::kTerrainRecipe, recipe.id, recipe.name, "texture_id");
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
    for (const auto& [entity_id, entity] : level.entities) {
      if (!Names(entity.sprite_id, sprite_id)) continue;
      Add(referrers, AssetKind::kLevel, level.id, level.name, absl::StrCat("entity ", entity_id));
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
    for (const auto& [entity_id, entity] : level.entities) {
      if (!Names(entity.collider_id, collider_id)) continue;
      Add(referrers, AssetKind::kLevel, level.id, level.name, absl::StrCat("entity ", entity_id));
    }
  }
  return referrers;
}

std::vector<AssetReference> FindBlueprintReferrers(const AssetCatalog& catalog,
                                                   std::string_view blueprint_id) {
  std::vector<AssetReference> referrers;
  if (blueprint_id.empty()) return referrers;

  for (const Level& level : catalog.levels) {
    for (const auto& [entity_id, entity] : level.entities) {
      if (!Names(entity.blueprint_id, blueprint_id)) continue;
      Add(referrers, AssetKind::kLevel, level.id, level.name, absl::StrCat("entity ", entity_id));
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
    for (const auto& [chunk_key, chunk] : level.tile_chunks) {
      for (const int painted_id : chunk.tiles) {
        if (painted_id == tile_id) ++painted;
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
