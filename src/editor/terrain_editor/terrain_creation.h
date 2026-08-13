#pragma once

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "api/api.h"
#include "resources/terrain_recipe_manager.h"
#include "terrain/terrain_generator.h"

namespace zebes {

// Where a newly authored terrain ended up, so the editor can tell the user
// rather than leaving them to guess which of the assets is theirs.
struct CreatedTerrain {
  std::string texture_id;
  std::string tileset_id;
  std::string recipe_id;
  int tile_count = 0;
};

// Renders an atlas, writes it into the assets tree as real artwork, and saves a
// new tileset holding the terrain.
//
// The tileset is created here rather than filled in, which is what removes the
// old requirement to have an empty tileset open first: a tileset names exactly
// one texture, so a terrain and the tileset that carries it are made together
// or not at all.
//
// Rendering a full atlas takes seconds; callers should expect this to block.
absl::StatusOr<CreatedTerrain> CreateGeneratedTerrainTileset(Api& api, const std::string& name,
                                                             const TerrainGenConfig& config);

// Creates the same assets and records the complete authoring input needed to
// reopen them. The source preset is provenance only; the recipe always carries
// the resolved full configuration.
absl::StatusOr<CreatedTerrain> CreateGeneratedTerrainTileset(
    Api& api, TerrainRecipeManager& recipes, const std::string& name,
    const TerrainGenConfig& config, const std::optional<std::string>& source_preset);

// Re-renders a recipe into its existing texture while preserving every asset
// and tile ID. Changes that alter atlas topology must use the creation overload
// above as Save As, because silently remapping IDs would break level data.
absl::Status RegenerateTerrainTileset(Api& api, TerrainRecipeManager& recipes,
                                      const TerrainRecipe& recipe, const TerrainGenConfig& config);

// The same, for artwork drawn by hand and composed by the compose_blob47 tool.
// The manifest describes an atlas that already exists, so its texture must have
// been imported first and is named here rather than written.
absl::StatusOr<CreatedTerrain> CreateImportedTerrainTileset(Api& api, const std::string& name,
                                                            const std::string& texture_id,
                                                            absl::string_view manifest_json);

}  // namespace zebes
