#pragma once

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "api/api.h"
#include "terrain/terrain_detect.h"
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

// The expensive, platform-neutral half of generated terrain creation. It owns
// the pixels and definitions until the editor thread commits them through Api.
// Keeping Api out of this value is the thread boundary: resource managers and
// GPU-backed texture state remain single-threaded.
struct PreparedGeneratedTerrain {
  Blob47Atlas atlas;
  TerrainCandidate candidate;
};

// Renders the complete atlas and builds its definitions without touching Api.
// Safe to run on a worker thread.
absl::StatusOr<PreparedGeneratedTerrain> PrepareGeneratedTerrain(const std::string& name,
                                                                 const TerrainGenConfig& config);

// Commits a prepared terrain on the thread that owns Api. The source preset is
// provenance only; the recipe always records the resolved configuration.
absl::StatusOr<CreatedTerrain> CommitGeneratedTerrain(
    Api& api, const std::string& name, const TerrainGenConfig& config,
    const std::optional<std::string>& source_preset, PreparedGeneratedTerrain prepared);

// Renders an atlas, writes it into the assets tree as real artwork, and saves a
// new tileset holding the terrain.
//
// The tileset is created here rather than filled in, which is what removes the
// old requirement to have an empty tileset open first: a tileset names exactly
// one texture, so a terrain and the tileset that carries it are made together
// or not at all.
//
// Synchronous convenience overload. Interactive callers should use Prepare on
// a worker and Commit on the editor thread instead.
absl::StatusOr<CreatedTerrain> CreateGeneratedTerrainTileset(Api& api, const std::string& name,
                                                             const TerrainGenConfig& config);

// Creates the same assets and records the complete authoring input needed to
// reopen them. The source preset is provenance only; the recipe always carries
// the resolved full configuration.
absl::StatusOr<CreatedTerrain> CreateGeneratedTerrainTileset(
    Api& api, const std::string& name, const TerrainGenConfig& config,
    const std::optional<std::string>& source_preset);

// Re-renders a recipe into its existing texture while preserving every asset
// and tile ID. Changes that alter atlas topology must use the creation overload
// above as Save As, because silently remapping IDs would break level data.
struct PreparedTerrainRegeneration {
  // The exact definition whose artwork was rendered. Commit refuses if it
  // changed while the worker was running, since a level may have appended
  // derived tiles in the meantime.
  Tileset source_tileset;
  Blob47Atlas atlas;
};

// Rejects edits that would change atlas topology and therefore tile source
// rectangles. Cheap enough to call before starting a worker.
absl::Status ValidateTerrainRegenerationConfig(const TerrainRecipe& recipe,
                                               const TerrainGenConfig& config);

// Validates and renders a snapshot without touching Api. Safe to run on a
// worker thread.
absl::StatusOr<PreparedTerrainRegeneration> PrepareTerrainRegeneration(
    Tileset tileset, const TerrainRecipe& recipe, const TerrainGenConfig& config);

// Saves a prepared redraw through Api, after verifying that its tileset
// snapshot is still current.
absl::Status CommitTerrainRegeneration(Api& api, const TerrainRecipe& recipe,
                                       const TerrainGenConfig& config,
                                       PreparedTerrainRegeneration prepared);

// Synchronous convenience wrapper around the two phases above.
absl::Status RegenerateTerrainTileset(Api& api, const TerrainRecipe& recipe,
                                      const TerrainGenConfig& config);

// The same, for artwork drawn by hand and composed by the compose_blob47 tool.
// The manifest describes an atlas that already exists, so its texture must have
// been imported first and is named here rather than written.
absl::StatusOr<CreatedTerrain> CreateImportedTerrainTileset(Api& api, const std::string& name,
                                                            const std::string& texture_id,
                                                            absl::string_view manifest_json);

}  // namespace zebes
