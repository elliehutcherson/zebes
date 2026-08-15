#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "objects/blueprint.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/tileset.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

// What kind of definition holds a reference.
enum class AssetKind {
  kTexture,
  kTileset,
  kSprite,
  kCollider,
  kBlueprint,
  kLevel,
  kTerrainRecipe,
};

// Human-facing name for a kind, for refusal messages.
std::string_view AssetKindName(AssetKind kind);

// One place a reference to some asset was found.
//
// Everything here describes the *referring* definition, because that is what the
// user has to go and change. `field` locates the reference inside it precisely
// enough to act on: "texture_id" is enough for a tileset, but a level may hold
// many themes, so a parallax reference names the theme and layer it sits in.
struct AssetReference {
  AssetKind kind = AssetKind::kTexture;
  std::string id;
  std::string display_name;
  std::string field;

  bool operator==(const AssetReference& other) const = default;
};

// Everything loaded, borrowed for the duration of one scan.
//
// Held by reference because a scan is a read: the caller owns the catalogues and
// outlives the call. A view that is missing a collection scans it as empty and
// therefore reports no references from it, which is why callers must pass every
// collection rather than only the ones they expect to matter.
struct AssetCatalog {
  const std::vector<Tileset>& tilesets;
  const std::vector<Sprite>& sprites;
  const std::vector<Blueprint>& blueprints;
  const std::vector<Level>& levels;
  const std::vector<TerrainRecipe>& recipes;
};

// Everything naming this texture: tilesets, sprites, and the parallax layers
// inside a level's themes. Empty IDs are not references -- an unset texture on a
// layer is a valid unfinished layer, not a pointer at nothing.
std::vector<AssetReference> FindTextureReferrers(const AssetCatalog& catalog,
                                                 std::string_view texture_id);

// Everything naming this tileset: levels that resolve their tiles through it,
// and the recipe that generated it.
std::vector<AssetReference> FindTilesetReferrers(const AssetCatalog& catalog,
                                                 std::string_view tileset_id);

std::vector<AssetReference> FindSpriteReferrers(const AssetCatalog& catalog,
                                                std::string_view sprite_id);

std::vector<AssetReference> FindColliderReferrers(const AssetCatalog& catalog,
                                                  std::string_view collider_id);

std::vector<AssetReference> FindBlueprintReferrers(const AssetCatalog& catalog,
                                                   std::string_view blueprint_id);

// Levels that have painted this tile.
//
// A level stores tile IDs as bare integers with no tileset qualifier, so the
// same integer means different artwork under a different tileset. The scan is
// therefore restricted to levels bound to `tileset_id`; without that it would
// report every level that happened to paint the same number.
std::vector<AssetReference> FindTileReferrers(const AssetCatalog& catalog,
                                              std::string_view tileset_id, int tile_id);

// Formats a refusal naming what the user has to change first. `subject` is what
// the caller tried to delete, spelled the way the UI spells it.
//
// Callers must not reach this with an empty list: "cannot delete, 0 things
// reference it" is a message no user can act on, and a delete with no referrers
// should have succeeded.
std::string DescribeBlockedDeletion(std::string_view subject,
                                    const std::vector<AssetReference>& referrers);

}  // namespace zebes
