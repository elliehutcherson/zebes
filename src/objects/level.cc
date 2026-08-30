#include "objects/level.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

bool Finite(Vec value) { return std::isfinite(value.x) && std::isfinite(value.y); }

enum class FadeAxis { kX, kY };

struct FadeSeam {
  const ParallaxZone* primary = nullptr;
  const ParallaxZone* secondary = nullptr;
  FadeAxis axis = FadeAxis::kX;
  double edge = 0.0;
  double primary_width = 0.0;
  double secondary_width = 0.0;
  double projection_min = 0.0;
  double projection_max = 0.0;
};

struct Bounds {
  Vec min;
  Vec max;
};

bool ContainsHalfOpen(const ParallaxZone& zone, Vec point) {
  return point.x >= zone.min_point.x && point.x < zone.max_point.x && point.y >= zone.min_point.y &&
         point.y < zone.max_point.y;
}

bool PositiveAreaOverlap(Bounds left, Bounds right) {
  return left.max.x > right.min.x && left.min.x < right.max.x && left.max.y > right.min.y &&
         left.min.y < right.max.y;
}

Bounds ZoneBounds(const ParallaxZone& zone) {
  return {.min = zone.min_point, .max = zone.max_point};
}

Bounds SeamBounds(const FadeSeam& seam) {
  if (seam.axis == FadeAxis::kX) {
    return {
        .min = {seam.edge - seam.primary_width, seam.projection_min},
        .max = {seam.edge + seam.secondary_width, seam.projection_max},
    };
  }
  return {
      .min = {seam.projection_min, seam.edge - seam.primary_width},
      .max = {seam.projection_max, seam.edge + seam.secondary_width},
  };
}

absl::Status ValidateResolvableZoneGeometry(const ParallaxZone& zone) {
  if (!Finite(zone.min_point) || !Finite(zone.max_point) || zone.min_point.x >= zone.max_point.x ||
      zone.min_point.y >= zone.max_point.y) {
    return absl::InvalidArgumentError(
        absl::StrCat("Zone '", zone.name, "' has invalid transition bounds."));
  }
  if (!Finite(zone.fade_length) || zone.fade_length.x < 0.0 || zone.fade_length.y < 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Zone '", zone.name, "' has invalid transition fade lengths."));
  }
  const double width = zone.max_point.x - zone.min_point.x;
  const double height = zone.max_point.y - zone.min_point.y;
  if (2.0 * zone.fade_length.x > width || 2.0 * zone.fade_length.y > height) {
    return absl::InvalidArgumentError(
        absl::StrCat("Zone '", zone.name, "' transition fade lengths exceed half its dimensions."));
  }
  return absl::OkStatus();
}

void AppendVerticalSeam(std::vector<FadeSeam>& seams, const ParallaxZone& left,
                        const ParallaxZone& right) {
  if (left.max_point.x != right.min_point.x) return;
  const double projection_min = std::max(left.min_point.y, right.min_point.y);
  const double projection_max = std::min(left.max_point.y, right.max_point.y);
  if (projection_min >= projection_max || left.fade_length.x + right.fade_length.x == 0.0) {
    return;
  }
  seams.push_back({
      .primary = &left,
      .secondary = &right,
      .axis = FadeAxis::kX,
      .edge = left.max_point.x,
      .primary_width = left.fade_length.x,
      .secondary_width = right.fade_length.x,
      .projection_min = projection_min,
      .projection_max = projection_max,
  });
}

void AppendHorizontalSeam(std::vector<FadeSeam>& seams, const ParallaxZone& top,
                          const ParallaxZone& bottom) {
  if (top.max_point.y != bottom.min_point.y) return;
  const double projection_min = std::max(top.min_point.x, bottom.min_point.x);
  const double projection_max = std::min(top.max_point.x, bottom.max_point.x);
  if (projection_min >= projection_max || top.fade_length.y + bottom.fade_length.y == 0.0) {
    return;
  }
  seams.push_back({
      .primary = &top,
      .secondary = &bottom,
      .axis = FadeAxis::kY,
      .edge = top.max_point.y,
      .primary_width = top.fade_length.y,
      .secondary_width = bottom.fade_length.y,
      .projection_min = projection_min,
      .projection_max = projection_max,
  });
}

absl::StatusOr<std::vector<FadeSeam>> BuildFadeSeams(const std::vector<ParallaxZone>& zones) {
  for (const ParallaxZone& zone : zones) {
    RETURN_IF_ERROR(ValidateResolvableZoneGeometry(zone));
  }

  std::vector<FadeSeam> seams;
  for (size_t left_index = 0; left_index < zones.size(); ++left_index) {
    for (size_t right_index = left_index + 1; right_index < zones.size(); ++right_index) {
      const ParallaxZone& first = zones[left_index];
      const ParallaxZone& second = zones[right_index];
      AppendVerticalSeam(seams, first, second);
      AppendVerticalSeam(seams, second, first);
      AppendHorizontalSeam(seams, first, second);
      AppendHorizontalSeam(seams, second, first);
    }
  }
  return seams;
}

bool ContainsHalfOpen(const FadeSeam& seam, Vec point) {
  const double coordinate = seam.axis == FadeAxis::kX ? point.x : point.y;
  const double projection = seam.axis == FadeAxis::kX ? point.y : point.x;
  const double start = seam.edge - seam.primary_width;
  const double end = seam.edge + seam.secondary_width;
  return coordinate >= start && coordinate < end && projection >= seam.projection_min &&
         projection < seam.projection_max;
}

double SecondaryWeight(const FadeSeam& seam, Vec point) {
  const double coordinate = seam.axis == FadeAxis::kX ? point.x : point.y;
  const double start = seam.edge - seam.primary_width;
  const double span = seam.primary_width + seam.secondary_width;
  return std::clamp((coordinate - start) / span, 0.0, 1.0);
}

absl::Status ValidateFadeSeams(const std::vector<FadeSeam>& seams,
                               const std::vector<ParallaxZone>& zones) {
  for (size_t seam_index = 0; seam_index < seams.size(); ++seam_index) {
    const FadeSeam& seam = seams[seam_index];
    const Bounds band = SeamBounds(seam);
    for (const ParallaxZone& zone : zones) {
      if (&zone == seam.primary || &zone == seam.secondary) continue;
      if (PositiveAreaOverlap(band, ZoneBounds(zone))) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Fade between zones '", seam.primary->name, "' and '", seam.secondary->name,
            "' passes through overlapping zone '", zone.name, "'."));
      }
    }
    for (size_t other_index = seam_index + 1; other_index < seams.size(); ++other_index) {
      if (!PositiveAreaOverlap(band, SeamBounds(seams[other_index]))) continue;
      const FadeSeam& other = seams[other_index];
      return absl::InvalidArgumentError(
          absl::StrCat("Fade bands for zones '", seam.primary->name, "' and '",
                       seam.secondary->name, "' intersect fade bands for zones '",
                       other.primary->name, "' and '", other.secondary->name, "'."));
    }
  }
  return absl::OkStatus();
}

bool ValidResourceId(std::string_view id) {
  if (id.empty()) return false;
  return std::all_of(id.begin(), id.end(), [](char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '_';
  });
}

absl::Status ValidateWorldGeometry(const Level& level) {
  if (!std::isfinite(level.width) || !std::isfinite(level.height) || level.width < 0.0 ||
      level.height < 0.0) {
    return absl::InvalidArgumentError("Level boundaries must be finite and non-negative.");
  }
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    return absl::InvalidArgumentError("Tile render dimensions must be positive.");
  }
  if (std::fmod(level.width, static_cast<double>(level.tile_render_width)) != 0.0 ||
      std::fmod(level.height, static_cast<double>(level.tile_render_height)) != 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Level boundaries must be multiples of tile render size (",
                     level.tile_render_width, " x ", level.tile_render_height, ")"));
  }
  if (!Finite(level.spawn_point) || level.spawn_point.x < 0 || level.spawn_point.y < 0 ||
      level.spawn_point.x > level.width || level.spawn_point.y > level.height) {
    return absl::InvalidArgumentError("Spawn point is outside level boundaries.");
  }
  return absl::OkStatus();
}

absl::Status ValidateThemesAndZones(const Level& level) {
  absl::flat_hash_set<int> zone_ids;
  for (const ParallaxZone& zone : level.zones) {
    if (zone.name.empty()) return absl::InvalidArgumentError("Zone name cannot be empty.");
    if (zone.id < 0) {
      return absl::InvalidArgumentError("Zone must have a valid non-negative integer ID.");
    }
    if (!zone_ids.insert(zone.id).second) {
      return absl::InvalidArgumentError(absl::StrCat("Duplicate zone ID found: '", zone.id, "'"));
    }
    if (!ValidResourceId(zone.theme_id)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone '", zone.name, "' must reference a valid parallax theme ID."));
    }
    if (!Finite(zone.min_point) || !Finite(zone.max_point) || zone.min_point.x < 0 ||
        zone.min_point.y < 0 || zone.max_point.x > level.width || zone.max_point.y > level.height) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone '", zone.name, "' extends outside level boundaries."));
    }
    if (zone.min_point.x >= zone.max_point.x || zone.min_point.y >= zone.max_point.y) {
      return absl::InvalidArgumentError(
          absl::StrCat("Zone '", zone.name, "' has invalid dimensions (min >= max)."));
    }
  }
  ASSIGN_OR_RETURN(const std::vector<FadeSeam> seams, BuildFadeSeams(level.zones));
  return ValidateFadeSeams(seams, level.zones);
}

absl::Status ValidateWorldLayers(const Level& level) {
  if (level.layers.empty()) {
    return absl::InvalidArgumentError("Level must contain at least one world layer.");
  }

  absl::flat_hash_set<int> layer_ids;
  absl::flat_hash_set<uint64_t> entity_ids;
  for (const WorldLayer& layer : level.layers) {
    if (layer.id < 0) {
      return absl::InvalidArgumentError("World layer must have a non-negative ID.");
    }
    if (!layer_ids.insert(layer.id).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate world layer ID found: '", layer.id, "'"));
    }
    if (layer.name.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("World layer ", layer.id, " must have a non-empty name."));
    }

    for (const auto& entry : layer.tile_chunks) {
      const TileChunkCoordinate coordinate = DecodeChunkKey(entry.first);
      if (coordinate.x < 0 || coordinate.y < 0) {
        return absl::InvalidArgumentError(absl::StrCat(
            "World layer '", layer.name, "' contains a tile chunk with negative coordinates."));
      }
      const TileChunk& chunk = entry.second;
      for (int index = 0; index < static_cast<int>(chunk.tiles.size()); ++index) {
        const int tile_id = chunk.tiles[index];
        if (tile_id < 0) {
          return absl::InvalidArgumentError(
              absl::StrCat("World layer '", layer.name, "' contains a negative tile ID."));
        }
        if (tile_id == 0) continue;

        const int64_t tile_x =
            static_cast<int64_t>(coordinate.x) * TileChunk::kSize + index % TileChunk::kSize;
        const int64_t tile_y =
            static_cast<int64_t>(coordinate.y) * TileChunk::kSize + index / TileChunk::kSize;
        if (static_cast<double>(tile_x + 1) * level.tile_render_width > level.width ||
            static_cast<double>(tile_y + 1) * level.tile_render_height > level.height) {
          return absl::InvalidArgumentError(absl::StrCat(
              "World layer '", layer.name, "' contains a tile outside level boundaries."));
        }
      }
    }

    for (const auto& [entity_id, entity] : layer.entities) {
      if (entity_id == Entity::kInvalidId || entity.id == Entity::kInvalidId) {
        return absl::InvalidArgumentError("Entity ID zero is invalid.");
      }
      if (entity_id != entity.id) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Entity map key '", entity_id, "' does not match entity id '", entity.id, "'"));
      }
      if (!entity_ids.insert(entity_id).second) {
        return absl::InvalidArgumentError(
            absl::StrCat("Duplicate entity ID found across world layers: '", entity_id, "'"));
      }
      if (entity.blueprint_id.empty() != entity.blueprint_state_key.empty()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Entity '", entity_id, "' must have both a Blueprint ID and state key, or neither."));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

int64_t ChunkKey(int chunk_x, int chunk_y) {
  const uint64_t encoded = (static_cast<uint64_t>(static_cast<uint32_t>(chunk_y)) << 32) |
                           static_cast<uint32_t>(chunk_x);
  return std::bit_cast<int64_t>(encoded);
}

TileChunkCoordinate DecodeChunkKey(int64_t key) {
  const uint64_t encoded = std::bit_cast<uint64_t>(key);
  return {
      .x = std::bit_cast<int32_t>(static_cast<uint32_t>(encoded)),
      .y = std::bit_cast<int32_t>(static_cast<uint32_t>(encoded >> 32)),
  };
}

absl::StatusOr<int> GetTileAt(const WorldLayer& layer, int tile_x, int tile_y) {
  if (tile_x < 0 || tile_y < 0) {
    return absl::InvalidArgumentError("tile coordinates must be non-negative");
  }

  constexpr int kSize = TileChunk::kSize;
  const int chunk_x = tile_x / kSize;
  const int chunk_y = tile_y / kSize;
  const int local_x = tile_x % kSize;
  const int local_y = tile_y % kSize;
  const auto chunk = layer.tile_chunks.find(ChunkKey(chunk_x, chunk_y));
  if (chunk == layer.tile_chunks.end()) return 0;
  return chunk->second.tiles[local_y * kSize + local_x];
}

WorldLayer* FindWorldLayer(Level& level, int layer_id) {
  for (WorldLayer& layer : level.layers) {
    if (layer.id == layer_id) return &layer;
  }
  return nullptr;
}

const WorldLayer* FindWorldLayer(const Level& level, int layer_id) {
  for (const WorldLayer& layer : level.layers) {
    if (layer.id == layer_id) return &layer;
  }
  return nullptr;
}

Entity* FindEntity(Level& level, uint64_t entity_id) {
  for (WorldLayer& layer : level.layers) {
    auto found = layer.entities.find(entity_id);
    if (found != layer.entities.end()) return &found->second;
  }
  return nullptr;
}

const Entity* FindEntity(const Level& level, uint64_t entity_id) {
  for (const WorldLayer& layer : level.layers) {
    auto found = layer.entities.find(entity_id);
    if (found != layer.entities.end()) return &found->second;
  }
  return nullptr;
}

WorldLayer* FindEntityLayer(Level& level, uint64_t entity_id) {
  for (WorldLayer& layer : level.layers) {
    if (layer.entities.contains(entity_id)) return &layer;
  }
  return nullptr;
}

const WorldLayer* FindEntityLayer(const Level& level, uint64_t entity_id) {
  for (const WorldLayer& layer : level.layers) {
    if (layer.entities.contains(entity_id)) return &layer;
  }
  return nullptr;
}

const ParallaxZone* FindParallaxZoneById(const std::vector<ParallaxZone>& zones, int zone_id) {
  auto found = std::find_if(zones.begin(), zones.end(),
                            [zone_id](const ParallaxZone& zone) { return zone.id == zone_id; });
  return found == zones.end() ? nullptr : &*found;
}

absl::StatusOr<std::optional<ResolvedParallaxEnvironment>> ResolveParallaxEnvironment(
    const std::vector<ParallaxZone>& zones, Vec reference_point) {
  if (!Finite(reference_point)) {
    return absl::InvalidArgumentError("Parallax environment reference point must be finite.");
  }
  ASSIGN_OR_RETURN(const std::vector<FadeSeam> seams, BuildFadeSeams(zones));

  const ParallaxZone* active = nullptr;
  for (auto zone = zones.rbegin(); zone != zones.rend(); ++zone) {
    if (ContainsHalfOpen(*zone, reference_point)) {
      active = &*zone;
      break;
    }
  }
  if (active == nullptr) return std::nullopt;

  const FadeSeam* resolved_seam = nullptr;
  for (const FadeSeam& seam : seams) {
    if (active != seam.primary && active != seam.secondary) continue;
    if (!ContainsHalfOpen(seam, reference_point)) continue;
    if (resolved_seam != nullptr) {
      return absl::FailedPreconditionError(
          absl::StrCat("Reference point resolves multiple parallax fade seams for active zone '",
                       active->name, "'."));
    }
    resolved_seam = &seam;
  }

  if (resolved_seam == nullptr) {
    return ResolvedParallaxEnvironment{
        .active_zone_id = active->id,
        .primary = {.zone_id = active->id, .theme_id = active->theme_id},
    };
  }

  const double secondary_weight = SecondaryWeight(*resolved_seam, reference_point);
  if (resolved_seam->primary->theme_id == resolved_seam->secondary->theme_id) {
    return ResolvedParallaxEnvironment{
        .active_zone_id = active->id,
        .primary = {.zone_id = resolved_seam->primary->id,
                    .theme_id = resolved_seam->primary->theme_id},
    };
  }
  if (secondary_weight >= 1.0) {
    return ResolvedParallaxEnvironment{
        .active_zone_id = active->id,
        .primary = {.zone_id = resolved_seam->secondary->id,
                    .theme_id = resolved_seam->secondary->theme_id},
    };
  }
  return ResolvedParallaxEnvironment{
      .active_zone_id = active->id,
      .primary = {.zone_id = resolved_seam->primary->id,
                  .theme_id = resolved_seam->primary->theme_id},
      .secondary = ResolvedParallaxTheme{.zone_id = resolved_seam->secondary->id,
                                         .theme_id = resolved_seam->secondary->theme_id},
      .secondary_weight = secondary_weight,
  };
}

absl::StatusOr<int> NextAvailableWorldLayerId(const Level& level) {
  int greatest = -1;
  for (const WorldLayer& layer : level.layers) greatest = std::max(greatest, layer.id);
  if (greatest == std::numeric_limits<int>::max()) {
    return absl::ResourceExhaustedError("No world layer IDs remain.");
  }
  return greatest + 1;
}

absl::StatusOr<uint64_t> NextAvailableEntityId(const Level& level) {
  uint64_t greatest = Entity::kInvalidId;
  bool found = false;
  for (const WorldLayer& layer : level.layers) {
    if (layer.entities.empty()) continue;
    greatest = std::max(greatest, layer.entities.rbegin()->first);
    found = true;
  }
  if (!found) return uint64_t{1};
  if (greatest == std::numeric_limits<uint64_t>::max()) {
    return absl::ResourceExhaustedError("No entity IDs remain.");
  }
  return greatest + 1;
}

absl::Status Level::AddEntity(int layer_id, Entity entity) {
  WorldLayer* layer = FindWorldLayer(*this, layer_id);
  if (layer == nullptr) {
    return absl::NotFoundError(absl::StrCat("World layer ", layer_id, " was not found."));
  }
  const uint64_t entity_id = entity.id;
  if (entity_id == Entity::kInvalidId) {
    return absl::InvalidArgumentError("Entity ID zero is invalid.");
  }
  if (FindEntity(*this, entity_id) != nullptr) {
    return absl::AlreadyExistsError(absl::StrCat("Entity ID ", entity_id, " already exists."));
  }
  layer->entities.emplace(entity_id, std::move(entity));
  return absl::OkStatus();
}

absl::Status MoveEntityToLayer(Level& level, uint64_t entity_id, int destination_layer_id) {
  WorldLayer* source = FindEntityLayer(level, entity_id);
  if (source == nullptr) {
    return absl::NotFoundError(absl::StrCat("Entity ", entity_id, " was not found."));
  }
  WorldLayer* destination = FindWorldLayer(level, destination_layer_id);
  if (destination == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("World layer ", destination_layer_id, " was not found."));
  }
  if (source == destination) return absl::OkStatus();

  auto node = source->entities.extract(entity_id);
  if (node.empty()) {
    return absl::InternalError("Entity disappeared while moving between world layers.");
  }
  destination->entities.insert(std::move(node));
  return absl::OkStatus();
}

absl::Status ValidateLevel(const Level& level) {
  if (level.id.empty()) return absl::InvalidArgumentError("Level must have an ID.");
  if (level.name.empty()) return absl::InvalidArgumentError("Level name cannot be empty.");
  RETURN_IF_ERROR(ValidateWorldGeometry(level));
  RETURN_IF_ERROR(ValidateThemesAndZones(level));
  return ValidateWorldLayers(level);
}

}  // namespace zebes
