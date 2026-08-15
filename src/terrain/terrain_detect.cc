#include "terrain/terrain_detect.h"

#include <map>
#include <optional>
#include <set>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"
#include "terrain/blob47_compose.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

// A cell position on the atlas grid, in tile units.
struct AtlasCoordinate {
  int column = 0;
  int row = 0;

  bool operator<(const AtlasCoordinate& other) const {
    if (row != other.row) return row < other.row;
    return column < other.column;
  }
};

// One manifest entry, before it is turned into a tile.
struct ManifestEntry {
  uint8_t mask = 0;
  int variant = 0;
  int source_x = 0;
  int source_y = 0;
};

// One slope unit from a manifest, before it is turned into a tile.
struct ManifestSlope {
  TileShape shape = TileShape::kNone;
  int source_x = 0;
  int source_y = 0;
};

absl::StatusOr<std::vector<ManifestSlope>> ParseManifestSlopes(const nlohmann::json& json) {
  std::vector<ManifestSlope> slopes;
  if (!json.contains("slopes") || !json["slopes"].is_array()) {
    return absl::InvalidArgumentError("terrain manifest has no slopes array");
  }

  try {
    for (const nlohmann::json& slope : json["slopes"]) {
      const std::string identifier = slope.at("shape").get<std::string>();
      const std::optional<TileShape> shape = TileShapeFromIdentifier(identifier);
      if (!shape.has_value()) {
        return absl::InvalidArgumentError(
            absl::StrCat("terrain manifest slope has unknown shape '", identifier, "'"));
      }
      // Two shapes a unit cannot be. kNone has no polygon and so no artwork to
      // point at, and kFullBlock is what the mask-keyed rules already draw, so a
      // unit claiming it would be a second, conflicting source for one shape.
      //
      // Every other shape is legal, half-blocks included. The numeric range this
      // replaces excluded them for no reason a scheme reading its own manifest
      // can defend: a unit is a shape drawn at full tile size, and a half-block
      // is one.
      if (*shape == TileShape::kNone || *shape == TileShape::kFullBlock) {
        return absl::InvalidArgumentError(
            absl::StrCat("terrain manifest slope has shape ", identifier,
                         ", which the mask-keyed rules already cover"));
      }
      slopes.push_back(ManifestSlope{
          .shape = *shape,
          .source_x = slope.at("source_x").get<int>(),
          .source_y = slope.at("source_y").get<int>(),
      });
    }
  } catch (const nlohmann::json::exception& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("malformed slope in terrain manifest: ", e.what()));
  }
  return slopes;
}

// Parses and scheme-checks the document once so tiles and slopes read from the
// same object.
absl::StatusOr<nlohmann::json> ParseManifestDocument(absl::string_view manifest_json) {
  nlohmann::json json = nlohmann::json::parse(manifest_json, nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded()) {
    return absl::InvalidArgumentError("terrain manifest is not valid JSON");
  }

  const std::string scheme = json.value("scheme", "");
  if (scheme != "blob47") {
    return absl::InvalidArgumentError(
        absl::StrCat("unsupported terrain manifest scheme '", scheme, "'; expected 'blob47'"));
  }
  return json;
}

absl::StatusOr<std::vector<ManifestEntry>> ParseManifest(const nlohmann::json& json) {
  if (!json.contains("tiles") || !json["tiles"].is_array()) {
    return absl::InvalidArgumentError("terrain manifest has no tiles array");
  }

  std::vector<ManifestEntry> entries;
  try {
    for (const nlohmann::json& tile : json["tiles"]) {
      entries.push_back(ManifestEntry{
          .mask = static_cast<uint8_t>(tile.at("mask").get<int>()),
          .variant = tile.at("variant").get<int>(),
          .source_x = tile.at("source_x").get<int>(),
          .source_y = tile.at("source_y").get<int>(),
      });
    }
  } catch (const nlohmann::json::exception& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("malformed entry in terrain manifest: ", e.what()));
  }
  return entries;
}

// Every mask in the table must be present for each variant, or the brush will
// hit a hole the first time a player paints that neighbourhood.
absl::Status ValidateManifestCoverage(const std::vector<ManifestEntry>& entries) {
  std::set<int> variants;
  std::set<std::pair<int, uint8_t>> seen;
  for (const ManifestEntry& entry : entries) {
    variants.insert(entry.variant);
    if (!Blob47IndexForMask(entry.mask).has_value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("terrain manifest contains non-normalized mask ",
                       static_cast<int>(entry.mask)));
    }
    if (!seen.insert({entry.variant, entry.mask}).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("terrain manifest repeats mask ", static_cast<int>(entry.mask),
                       " in variant ", entry.variant));
    }
  }

  const size_t expected = variants.size() * kBlob47TileCount;
  if (entries.size() != expected) {
    return absl::InvalidArgumentError(absl::StrCat("terrain manifest describes ", entries.size(),
                                                   " cells; expected ", expected, " for ",
                                                   variants.size(), " variant(s)"));
  }
  return absl::OkStatus();
}

// Indexes a tileset's tiles by their atlas cell. Tiles whose source position is
// not cell-aligned cannot belong to a generated block and are skipped.
std::map<AtlasCoordinate, int> BuildAtlasIndex(const Tileset& tileset) {
  std::map<AtlasCoordinate, int> index;
  for (const Tile& tile : tileset.tiles) {
    if (tile.source_x % tileset.tile_width != 0) continue;
    if (tile.source_y % tileset.tile_height != 0) continue;
    index[AtlasCoordinate{.column = tile.source_x / tileset.tile_width,
                          .row = tile.source_y / tileset.tile_height}] = tile.id;
  }
  return index;
}

// Returns the tile IDs of a complete blob-47 block at the given origin, in
// table order, or nullopt when any of the 47 cells is missing.
std::optional<std::vector<int>> ReadBlockAt(const std::map<AtlasCoordinate, int>& index,
                                            int origin_column, int origin_row) {
  std::vector<int> tile_ids;
  tile_ids.reserve(kBlob47TileCount);

  for (int i = 0; i < kBlob47TileCount; ++i) {
    const AtlasCoordinate coordinate{
        .column = origin_column + i % kBlob47Columns,
        .row = origin_row + i / kBlob47Columns,
    };
    auto found = index.find(coordinate);
    if (found == index.end()) return std::nullopt;
    tile_ids.push_back(found->second);
  }
  return tile_ids;
}

// Derives a terrain name from the tiles' shared name prefix so detected
// candidates arrive with something better than "Terrain 1".
std::string SuggestName(const Tileset& tileset, const std::vector<int>& tile_ids) {
  absl::flat_hash_map<int, const Tile*> by_id;
  for (const Tile& tile : tileset.tiles) by_id[tile.id] = &tile;

  std::string prefix;
  bool first = true;
  for (int tile_id : tile_ids) {
    auto found = by_id.find(tile_id);
    if (found == by_id.end()) continue;
    const std::string& name = found->second->name;
    if (first) {
      prefix = name;
      first = false;
      continue;
    }
    size_t shared = 0;
    while (shared < prefix.size() && shared < name.size() && prefix[shared] == name[shared]) {
      ++shared;
    }
    prefix.resize(shared);
  }

  while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == '-' || prefix.back() == ' ')) {
    prefix.pop_back();
  }
  if (prefix.empty()) return "Terrain";
  return prefix;
}

// Appends one variant's tile IDs into a rule table keyed by mask.
void AppendVariant(const std::vector<int>& tile_ids, std::vector<TerrainRule>& rules) {
  absl::Span<const uint8_t> masks = Blob47MaskTable();
  for (int i = 0; i < kBlob47TileCount; ++i) {
    if (static_cast<int>(rules.size()) <= i) {
      rules.push_back(TerrainRule{.mask = masks[i]});
    }
    rules[i].variants.push_back(TerrainVariant{.tile_id = tile_ids[i], .weight = 1});
  }
}

}  // namespace

absl::StatusOr<TerrainCandidate> BuildTerrainCandidate(const Blob47Atlas& atlas, int first_tile_id,
                                                       int terrain_id, TerrainScheme scheme) {
  if (first_tile_id <= 0) {
    return absl::InvalidArgumentError("first tile ID must be positive; 0 is reserved for empty");
  }

  TerrainCandidate candidate;
  candidate.suggested_name = "Terrain";
  candidate.tile_size = atlas.tile_size;
  candidate.terrain.id = terrain_id;
  candidate.terrain.name = candidate.suggested_name;
  candidate.terrain.scheme = scheme;
  candidate.terrain.variant_period = atlas.variant_period;

  // Rules are keyed by mask and accumulate one variant per atlas variant.
  std::map<uint8_t, std::vector<TerrainVariant>> variants_by_mask;
  int next_tile_id = first_tile_id;
  for (const ComposedTile& tile : atlas.tiles) {
    const int tile_id = next_tile_id++;
    candidate.tiles.push_back(Tile{
        .id = tile_id,
        .name = absl::StrCat("Mask", static_cast<int>(tile.mask), "_v", tile.variant),
        .source_x = tile.source_x,
        .source_y = tile.source_y,
        .shape = TileShape::kFullBlock,
    });
    variants_by_mask[tile.mask].push_back(TerrainVariant{.tile_id = tile_id, .weight = 1});
  }

  // A derived terrain gets no rule table. Resolving a full block by mask is
  // exactly the lossy step this scheme exists to remove -- a mask cannot say
  // that the neighbour is a wedge, which is why ground beside a ramp was banded
  // against a square that was not there. Its tiles are still real artwork for
  // the all-square neighbourhoods they depict, so they are listed as owned and
  // the renderer finds them again by content whenever a key draws the same
  // picture.
  if (scheme == TerrainScheme::kDerived) {
    // Each generated tile records the neighbourhood it depicts: a full block
    // whose neighbours are all full blocks in whichever positions its mask
    // names. That is the same input the renderer was given, so regeneration can
    // redraw it exactly rather than trusting that the layout has not moved.
    for (size_t i = 0; i < candidate.tiles.size(); ++i) {
      const ComposedTile& composed = atlas.tiles[i];
      TerrainCellKey key;
      key.shape = TileShape::kFullBlock;
      for (int bit = 0; bit < kNeighborCount; ++bit) {
        key.neighbors[bit] = (composed.mask & (1 << bit)) != 0 ? TileShape::kFullBlock
                                                               : TileShape::kNone;
      }
      key.phase = composed.variant;
      candidate.terrain.derived_tiles.push_back(
          DerivedTile{.tile_id = candidate.tiles[i].id, .key = key});
    }
  } else {
    for (auto& [mask, variants] : variants_by_mask) {
      candidate.terrain.rules.push_back(
          TerrainRule{.mask = mask, .variants = std::move(variants)});
    }
  }

  // Slope units are placed by hand, never by the brush, so they become terrain
  // members rather than rules. Registering them here is what stops painted
  // ground from capping off with an edge against a slope.
  for (const ComposedSlope& slope : atlas.slopes) {
    const int tile_id = next_tile_id++;
    candidate.tiles.push_back(Tile{
        .id = tile_id,
        .name = absl::StrCat("Slope", static_cast<int>(slope.shape)),
        .source_x = slope.source_x,
        .source_y = slope.source_y,
        .shape = slope.shape,
    });
    candidate.terrain.shape_tile_ids.push_back(tile_id);
  }

  return candidate;
}

absl::StatusOr<TerrainCandidate> ImportBlob47Manifest(absl::string_view manifest_json,
                                                      int first_tile_id, int terrain_id) {
  ASSIGN_OR_RETURN(nlohmann::json json, ParseManifestDocument(manifest_json));
  ASSIGN_OR_RETURN(std::vector<ManifestEntry> entries, ParseManifest(json));
  RETURN_IF_ERROR(ValidateManifestCoverage(entries));
  ASSIGN_OR_RETURN(std::vector<ManifestSlope> slopes, ParseManifestSlopes(json));

  // The manifest describes an atlas that was written earlier; rebuilding that
  // description lets the generated and composed paths share everything past
  // this point.
  Blob47Atlas atlas;
  if (!json.contains("tile_size")) {
    return absl::InvalidArgumentError(
        "terrain manifest has no tile_size; regenerate it with a current tool");
  }
  atlas.tile_size = json.at("tile_size").get<int>();
  if (!json.contains("variant_period")) {
    return absl::InvalidArgumentError(
        "terrain manifest has no variant_period; regenerate it with a current tool");
  }
  atlas.variant_period = json.at("variant_period").get<int>();
  if (atlas.variant_period < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("terrain manifest has a negative variant period: ", atlas.variant_period));
  }
  for (const ManifestEntry& entry : entries) {
    atlas.tiles.push_back(ComposedTile{
        .mask = entry.mask,
        .variant = entry.variant,
        .source_x = entry.source_x,
        .source_y = entry.source_y,
    });
  }
  for (const ManifestSlope& slope : slopes) {
    atlas.slopes.push_back(ComposedSlope{
        .shape = slope.shape,
        .source_x = slope.source_x,
        .source_y = slope.source_y,
    });
  }

  // A manifest describes artwork somebody drew, so it is complete as imported.
  return BuildTerrainCandidate(atlas, first_tile_id, terrain_id, TerrainScheme::kBlob47);
}

absl::StatusOr<std::vector<TerrainCandidate>> DetectBlob47Terrains(const Tileset& tileset) {
  if (tileset.tile_width <= 0 || tileset.tile_height <= 0) {
    return absl::InvalidArgumentError("tileset tile dimensions must be positive");
  }

  const std::map<AtlasCoordinate, int> index = BuildAtlasIndex(tileset);
  std::set<AtlasCoordinate> claimed;
  std::vector<TerrainCandidate> candidates;
  int next_terrain_id = 1;

  for (const auto& [origin, unused_tile_id] : index) {
    if (claimed.count(origin) == 1) continue;

    std::optional<std::vector<int>> block = ReadBlockAt(index, origin.column, origin.row);
    if (!block.has_value()) continue;

    TerrainCandidate candidate;
    candidate.terrain.id = next_terrain_id++;
    candidate.terrain.scheme = TerrainScheme::kBlob47;

    // Blocks stacked directly below are additional variants of one terrain.
    int variant_row = origin.row;
    std::vector<int> first_block_tiles = *block;
    while (block.has_value()) {
      AppendVariant(*block, candidate.terrain.rules);
      for (int i = 0; i < kBlob47TileCount; ++i) {
        claimed.insert(AtlasCoordinate{
            .column = origin.column + i % kBlob47Columns,
            .row = variant_row + i / kBlob47Columns,
        });
      }
      variant_row += kBlob47Rows;
      block = ReadBlockAt(index, origin.column, variant_row);
    }

    candidate.suggested_name = SuggestName(tileset, first_block_tiles);
    candidate.terrain.name = candidate.suggested_name;
    candidates.push_back(std::move(candidate));
  }

  return candidates;
}

}  // namespace zebes
