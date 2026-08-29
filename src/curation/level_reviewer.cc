#include "curation/level_reviewer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "curation/level_review_route.h"
#include "curation/prop_candidate.h"
#include "curation/raster_canvas.h"
#include "editor/level_editor/parallax_layout.h"
#include "editor/level_editor/viewport_scene.h"
#include "editor/parallax_theme_editor/parallax_diagnostics.h"
#include "editor/parallax_theme_editor/parallax_preview_model.h"
#include "objects/parallax_theme.h"
#include "resources/level_manager.h"
#include "resources/tileset_manager.h"

namespace zebes {
namespace {

constexpr std::array<double, 3> kReviewZooms = {0.5, 1.0, 2.0};
constexpr int kContactColumns = 4;
constexpr int kContactWidth = 240;
constexpr int kContactGutter = 4;
constexpr int kLayoutMaximumWidth = 2048;
constexpr int kLayoutMaximumHeight = 1024;

constexpr RgbaColor8 kBackground{.red = 7, .green = 0, .blue = 13, .alpha = 255};
constexpr RgbaColor8 kTransparent{.red = 0, .green = 0, .blue = 0, .alpha = 0};
constexpr RgbaColor8 kCheckerLight{.red = 55, .green = 55, .blue = 65, .alpha = 255};
constexpr RgbaColor8 kCheckerDark{.red = 35, .green = 35, .blue = 45, .alpha = 255};
constexpr RgbaColor8 kPlaceholder{.red = 100, .green = 100, .blue = 200, .alpha = 180};
constexpr RgbaColor8 kZoneOutline{.red = 230, .green = 185, .blue = 40, .alpha = 255};
constexpr RgbaColor8 kSpawn{.red = 80, .green = 255, .blue = 120, .alpha = 255};
constexpr RgbaColor8 kFocus{.red = 255, .green = 220, .blue = 40, .alpha = 255};
constexpr std::array<RgbaColor8, 3> kRouteColors = {
    RgbaColor8{.red = 70, .green = 210, .blue = 255, .alpha = 255},
    RgbaColor8{.red = 210, .green = 120, .blue = 255, .alpha = 255},
    RgbaColor8{.red = 255, .green = 160, .blue = 80, .alpha = 255},
};
constexpr std::array<RgbaColor8, 6> kLayerColors = {
    RgbaColor8{.red = 100, .green = 165, .blue = 255, .alpha = 255},
    RgbaColor8{.red = 160, .green = 230, .blue = 100, .alpha = 255},
    RgbaColor8{.red = 255, .green = 130, .blue = 100, .alpha = 255},
    RgbaColor8{.red = 210, .green = 120, .blue = 255, .alpha = 255},
    RgbaColor8{.red = 255, .green = 220, .blue = 100, .alpha = 255},
    RgbaColor8{.red = 100, .green = 230, .blue = 215, .alpha = 255},
};

struct LoadedTexture {
  TextureHandle handle;
  RgbaImage pixels;
};

struct LevelReviewAssets {
  std::optional<Tileset> tileset;
  std::map<std::string, ParallaxTheme, std::less<>> themes;
  std::map<std::string, Sprite, std::less<>> sprites;
  std::map<std::string, LoadedTexture, std::less<>> textures;
  std::map<uint64_t, std::string> texture_ids_by_handle;
  SpriteLookup sprite_lookup;
};

struct FocusedEntityContext {
  uint64_t id = Entity::kInvalidId;
  int layer_id = 0;
  std::string layer_name;
  Vec position;
  WorldRect bounds;
  const Entity* entity = nullptr;
};

struct TransientEntityReplacement {
  uint64_t entity_id = Entity::kInvalidId;
  std::string persisted_sprite_id;
  const PreparedPropCandidate* candidate = nullptr;
};

struct EntityRaster {
  const RgbaImage* texture = nullptr;
  RasterSourceRect source;
  bool transient_replacement = false;
};

struct WorldRenderStats {
  size_t tile_count = 0;
  std::vector<uint64_t> entity_ids;
  std::vector<uint64_t> transient_replacement_entity_ids;
};

struct RenderedLevelFrame {
  RgbaImage image;
  std::optional<ResolvedParallaxEnvironment> environment;
  WorldRenderStats world;
  std::vector<std::string> render_sequence;
};

struct AlphaMetrics {
  bool occupied = false;
  int min_x = 0;
  int min_y = 0;
  int max_x = -1;
  int max_y = -1;
  int empty_left = 0;
  int empty_top = 0;
  int empty_right = 0;
  int empty_bottom = 0;
};

struct ThemeLayerSelection {
  std::string theme_id;
  int layer_index = -1;
};

struct ContactSheetAccumulator {
  RgbaImage image;
  std::vector<std::string> ordered_artifact_ids;
  size_t frame_count = 0;
  size_t next_frame = 0;
  int columns = 0;
  int thumbnail_height = 0;
};

class CollectingArtifactSink final : public CurationArtifactSink {
 public:
  absl::Status Add(const CurationArtifact& artifact) override {
    artifacts.push_back(artifact);
    return absl::OkStatus();
  }

  std::vector<CurationArtifact> artifacts;
};

std::string ZoomId(double zoom) {
  return absl::StrFormat("z%03d", static_cast<int>(std::lround(zoom * 100.0)));
}

nlohmann::json VecToJson(Vec value) { return {{"x", value.x}, {"y", value.y}}; }

nlohmann::json VisibleBoundsToJson(const Camera& camera) {
  const VisibleWorldBounds visible = CalculateVisibleWorldBounds(camera);
  return {{"min", VecToJson(visible.min)}, {"max", VecToJson(visible.max)}};
}

bool HasTiles(const Level& level) {
  for (const WorldLayer& layer : level.layers) {
    for (const auto& [unused, chunk] : layer.tile_chunks) {
      static_cast<void>(unused);
      if (std::any_of(chunk.tiles.begin(), chunk.tiles.end(),
                      [](int tile_id) { return tile_id != 0; })) {
        return true;
      }
    }
  }
  return false;
}

absl::StatusOr<LoadedTexture*> LoadStrictTexture(Api& api, const std::string& texture_id,
                                                 LevelReviewAssets& assets) {
  if (texture_id.empty()) {
    return absl::FailedPreconditionError("level review encountered an empty texture ID");
  }
  auto existing = assets.textures.find(texture_id);
  if (existing != assets.textures.end()) return &existing->second;

  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(texture_id));
  if (texture == nullptr || texture->id != texture_id) {
    return absl::FailedPreconditionError(
        absl::StrCat("texture lookup returned invalid data: ", texture_id));
  }
  ASSIGN_OR_RETURN(const TextureHandle handle, api.GetTextureHandle(texture_id));
  if (!handle) {
    return absl::FailedPreconditionError(
        absl::StrCat("texture has no headless resource handle: ", texture_id));
  }
  ASSIGN_OR_RETURN(RgbaImage pixels, api.ReadTexturePixels(texture_id));
  if (!pixels.IsValid()) {
    return absl::DataLossError(absl::StrCat("texture pixels are invalid: ", texture_id));
  }
  auto handle_owner = assets.texture_ids_by_handle.find(handle.id());
  if (handle_owner != assets.texture_ids_by_handle.end() && handle_owner->second != texture_id) {
    return absl::FailedPreconditionError("two level textures resolved to the same handle");
  }
  assets.texture_ids_by_handle.emplace(handle.id(), texture_id);
  auto [inserted, unused] = assets.textures.emplace(
      texture_id, LoadedTexture{.handle = handle, .pixels = std::move(pixels)});
  static_cast<void>(unused);
  return &inserted->second;
}

const LoadedTexture* FindTexture(const LevelReviewAssets& assets, TextureHandle handle) {
  auto id = assets.texture_ids_by_handle.find(handle.id());
  if (id == assets.texture_ids_by_handle.end()) return nullptr;
  auto texture = assets.textures.find(id->second);
  return texture == assets.textures.end() ? nullptr : &texture->second;
}

absl::Status ValidateSpriteFrame(const Sprite& sprite, const LoadedTexture& texture) {
  if (sprite.frames.empty()) return absl::OkStatus();
  const SpriteFrame& frame = sprite.frames.front();
  const int64_t right = static_cast<int64_t>(frame.texture_x) + frame.texture_w;
  const int64_t bottom = static_cast<int64_t>(frame.texture_y) + frame.texture_h;
  if (frame.texture_x < 0 || frame.texture_y < 0 || frame.texture_w <= 0 || frame.texture_h <= 0 ||
      frame.render_w <= 0 || frame.render_h <= 0 || right > texture.pixels.width ||
      bottom > texture.pixels.height) {
    return absl::FailedPreconditionError(
        absl::StrCat("sprite '", sprite.name, "' has an invalid first frame"));
  }
  return absl::OkStatus();
}

absl::StatusOr<LevelReviewAssets> LoadAssets(Api& api, const Level& level) {
  LevelReviewAssets assets;
  if (!level.tileset_id.empty()) {
    ASSIGN_OR_RETURN(Tileset * tileset, api.GetTileset(level.tileset_id));
    if (tileset == nullptr || tileset->id != level.tileset_id) {
      return absl::FailedPreconditionError("level tileset lookup returned invalid data");
    }
    RETURN_IF_ERROR(ValidateTileset(*tileset));
    assets.tileset = *tileset;
    if (!tileset->texture_id.empty()) {
      RETURN_IF_ERROR(LoadStrictTexture(api, tileset->texture_id, assets).status());
    } else if (HasTiles(level)) {
      return absl::FailedPreconditionError("level has painted tiles but its tileset has no atlas");
    }
  } else if (HasTiles(level)) {
    return absl::FailedPreconditionError("level has painted tiles but no tileset");
  }

  std::set<std::string> theme_ids;
  for (const ParallaxZone& zone : level.zones) theme_ids.insert(zone.theme_id);
  for (const std::string& theme_id : theme_ids) {
    ASSIGN_OR_RETURN(ParallaxTheme * theme, api.GetParallaxTheme(theme_id));
    if (theme == nullptr || theme->id != theme_id) {
      return absl::FailedPreconditionError(
          absl::StrCat("level theme lookup returned invalid data: ", theme_id));
    }
    RETURN_IF_ERROR(ValidateParallaxTheme(*theme));
    assets.themes.emplace(theme_id, *theme);
    for (const ParallaxLayer& layer : theme->layers) {
      for (const ParallaxElement& element : layer.elements) {
        RETURN_IF_ERROR(LoadStrictTexture(api, element.texture_id, assets).status());
      }
    }
  }

  std::set<std::string> sprite_ids;
  for (const WorldLayer& layer : level.layers) {
    for (const auto& [unused, entity] : layer.entities) {
      static_cast<void>(unused);
      if (!entity.sprite_id.empty()) sprite_ids.insert(entity.sprite_id);
    }
  }
  for (const std::string& sprite_id : sprite_ids) {
    ASSIGN_OR_RETURN(Sprite * loaded_sprite, api.GetSprite(sprite_id));
    if (loaded_sprite == nullptr || loaded_sprite->id != sprite_id) {
      return absl::FailedPreconditionError(
          absl::StrCat("sprite lookup returned invalid data: ", sprite_id));
    }
    assets.sprites.emplace(sprite_id, *loaded_sprite);
    const Sprite& sprite = assets.sprites.at(sprite_id);
    if (sprite.frames.empty() || sprite.texture_id.empty()) {
      return absl::FailedPreconditionError(
          absl::StrCat("referenced sprite has no renderable first frame: ", sprite.name));
    }
    ASSIGN_OR_RETURN(LoadedTexture * texture, LoadStrictTexture(api, sprite.texture_id, assets));
    RETURN_IF_ERROR(ValidateSpriteFrame(sprite, *texture));
  }

  for (auto& [sprite_id, sprite] : assets.sprites) {
    TextureHandle handle;
    auto texture = assets.textures.find(sprite.texture_id);
    if (!sprite.frames.empty() && texture != assets.textures.end()) handle = texture->second.handle;
    assets.sprite_lookup.emplace(sprite_id, ResolvedSprite{.sprite = &sprite, .texture = handle});
  }
  return assets;
}

absl::StatusOr<TransientEntityReplacement> ApplyTransientPropCandidate(
    Level& preview_level, LevelReviewAssets& assets, uint64_t entity_id,
    const PreparedPropCandidate& candidate) {
  RETURN_IF_ERROR(ValidatePreparedPropCandidate(candidate));
  Entity* target = nullptr;
  for (WorldLayer& layer : preview_level.layers) {
    auto found = layer.entities.find(entity_id);
    if (found == layer.entities.end()) continue;
    if (target != nullptr) {
      return absl::FailedPreconditionError(
          absl::StrCat("transient replacement entity appears in multiple layers: ", entity_id));
    }
    target = &found->second;
  }
  if (target == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("transient replacement entity was not found: ", entity_id));
  }

  const std::string persisted_sprite_id = target->sprite_id;
  const std::string base_id = absl::StrCat("__curation_transient_prop_", entity_id);
  std::string preview_sprite_id = base_id;
  for (size_t suffix = 1; assets.sprites.contains(preview_sprite_id); ++suffix) {
    preview_sprite_id = absl::StrCat(base_id, "_", suffix);
  }
  Sprite preview_sprite = candidate.sprite;
  preview_sprite.id = preview_sprite_id;
  preview_sprite.texture_id = absl::StrCat(preview_sprite_id, "_texture");
  auto [inserted, unique] = assets.sprites.emplace(preview_sprite_id, std::move(preview_sprite));
  if (!unique) {
    return absl::InternalError("transient preview sprite ID allocation was not unique");
  }
  const auto [unused, lookup_unique] =
      assets.sprite_lookup.emplace(preview_sprite_id, ResolvedSprite{.sprite = &inserted->second});
  static_cast<void>(unused);
  if (!lookup_unique) {
    return absl::InternalError("transient preview sprite lookup was not unique");
  }
  target->sprite_id = preview_sprite_id;
  return TransientEntityReplacement{
      .entity_id = entity_id,
      .persisted_sprite_id = persisted_sprite_id,
      .candidate = &candidate,
  };
}

absl::StatusOr<FocusedEntityContext> FindFocusedEntity(const Level& level, uint64_t entity_id) {
  if (entity_id == Entity::kInvalidId) {
    return absl::InvalidArgumentError("focused level review entity ID is invalid");
  }
  const WorldLayer* matched_layer = nullptr;
  const Entity* matched_entity = nullptr;
  for (const WorldLayer& layer : level.layers) {
    const auto found = layer.entities.find(entity_id);
    if (found == layer.entities.end()) continue;
    if (matched_entity != nullptr) {
      return absl::FailedPreconditionError(
          absl::StrCat("focused entity ID appears in multiple world layers: ", entity_id));
    }
    matched_layer = &layer;
    matched_entity = &found->second;
  }
  if (matched_entity == nullptr || matched_layer == nullptr) {
    return absl::NotFoundError(absl::StrCat("focused entity was not found: ", entity_id));
  }
  if (!matched_entity->active) {
    return absl::FailedPreconditionError(absl::StrCat("focused entity is inactive: ", entity_id));
  }
  return FocusedEntityContext{
      .id = entity_id,
      .layer_id = matched_layer->id,
      .layer_name = matched_layer->name,
      .position = matched_entity->transform.position,
      .entity = matched_entity,
  };
}

absl::Status ResolveFocusedEntityBounds(const LevelReviewAssets& assets,
                                        FocusedEntityContext& focus) {
  if (focus.entity == nullptr) {
    return absl::InternalError("focused entity context lost its entity definition");
  }
  const ResolvedSprite resolved = FindSprite(assets.sprite_lookup, focus.entity->sprite_id);
  ASSIGN_OR_RETURN(focus.bounds, CalculateEntityBounds(*focus.entity, resolved.sprite));
  return absl::OkStatus();
}

nlohmann::json FocusToJson(const FocusedEntityContext& focus) {
  return {
      {"entity_id", focus.id},
      {"world_layer_id", focus.layer_id},
      {"world_layer_name", focus.layer_name},
      {"position", VecToJson(focus.position)},
      {"bounds", {{"min", VecToJson(focus.bounds.min)}, {"max", VecToJson(focus.bounds.max)}}},
  };
}

const ParallaxElement* FindElement(const ParallaxLayer& layer, int element_id) {
  for (const ParallaxElement& element : layer.elements) {
    if (element.id == element_id) return &element;
  }
  return nullptr;
}

absl::Status CompositeParallaxTheme(RgbaImage& canvas, const LevelReviewAssets& assets,
                                    const ParallaxTheme& theme, const Camera& camera,
                                    double opacity, std::optional<int> layer_index = std::nullopt) {
  std::map<std::string, TextureHandle> handles;
  for (const ParallaxLayer& layer : theme.layers) {
    for (const ParallaxElement& element : layer.elements) {
      auto texture = assets.textures.find(element.texture_id);
      if (texture == assets.textures.end()) {
        return absl::FailedPreconditionError(
            absl::StrCat("parallax texture was not loaded: ", element.texture_id));
      }
      handles.emplace(element.texture_id, texture->second.handle);
    }
  }
  ASSIGN_OR_RETURN(const ParallaxRenderBatch batch,
                   ComposeParallaxRenderBatch(theme, camera, handles,
                                              {.opacity = opacity, .layer_index = layer_index}));
  for (const ParallaxRenderItem& item : batch.layers) {
    std::vector<ParallaxElementSize> sizes;
    sizes.reserve(item.elements.size());
    for (const ParallaxElementRenderResource& resource : item.elements) {
      const LoadedTexture* texture = FindTexture(assets, resource.texture);
      if (texture == nullptr) {
        return absl::FailedPreconditionError("parallax handle has no loaded pixels");
      }
      sizes.push_back({.element_id = resource.element_id,
                       .width = texture->pixels.width,
                       .height = texture->pixels.height});
    }
    ASSIGN_OR_RETURN(const ParallaxLayout layout,
                     CalculateParallaxLayout(camera, item.layer, sizes));
    for (const ParallaxElementLayout& placed : layout.elements) {
      const ParallaxElement* element = FindElement(item.layer, placed.element_id);
      if (element == nullptr) {
        return absl::InternalError("parallax layout referenced an unknown element");
      }
      const LoadedTexture& texture = assets.textures.at(element->texture_id);
      const Vec minimum = camera.WorldToScreen(placed.bounds.min);
      const Vec maximum = camera.WorldToScreen(placed.bounds.max);
      RETURN_IF_ERROR(CompositeRgbaNearest(
          canvas, texture.pixels,
          {.x = 0, .y = 0, .width = texture.pixels.width, .height = texture.pixels.height},
          {.x = minimum.x,
           .y = minimum.y,
           .width = maximum.x - minimum.x,
           .height = maximum.y - minimum.y},
          opacity));
    }
  }
  return absl::OkStatus();
}

absl::Status CompositeEnvironment(RgbaImage& canvas, const LevelReviewAssets& assets,
                                  const Camera& camera,
                                  const std::optional<ResolvedParallaxEnvironment>& environment,
                                  std::optional<ThemeLayerSelection> selection = std::nullopt) {
  if (selection.has_value()) {
    auto theme = assets.themes.find(selection->theme_id);
    if (theme == assets.themes.end()) {
      return absl::FailedPreconditionError("isolated parallax theme is unavailable");
    }
    return CompositeParallaxTheme(canvas, assets, theme->second, camera, 1.0,
                                  selection->layer_index);
  }
  if (!environment.has_value()) return absl::OkStatus();
  auto primary = assets.themes.find(environment->primary.theme_id);
  if (primary == assets.themes.end()) {
    return absl::FailedPreconditionError("resolved primary parallax theme is unavailable");
  }
  RETURN_IF_ERROR(CompositeParallaxTheme(canvas, assets, primary->second, camera, 1.0));
  if (!environment->secondary.has_value() || environment->secondary_weight == 0.0) {
    return absl::OkStatus();
  }
  auto secondary = assets.themes.find(environment->secondary->theme_id);
  if (secondary == assets.themes.end()) {
    return absl::FailedPreconditionError("resolved secondary parallax theme is unavailable");
  }
  return CompositeParallaxTheme(canvas, assets, secondary->second, camera,
                                environment->secondary_weight);
}

bool IntersectsScreen(const Camera& camera, const WorldRect& bounds) {
  const Vec minimum = camera.WorldToScreen(bounds.min);
  const Vec maximum = camera.WorldToScreen(bounds.max);
  return maximum.x > 0.0 && maximum.y > 0.0 && minimum.x < camera.viewport_width &&
         minimum.y < camera.viewport_height;
}

absl::StatusOr<std::optional<EntityRaster>> ResolveEntityRaster(
    const LevelReviewAssets& assets, const EntityRenderItem& item,
    const TransientEntityReplacement* replacement) {
  if (replacement != nullptr && item.entity_id == replacement->entity_id) {
    if (replacement->candidate == nullptr || replacement->candidate->sprite.frames.empty()) {
      return absl::InternalError("transient entity replacement lost its prepared artwork");
    }
    const SpriteFrame& frame = replacement->candidate->sprite.frames.front();
    return EntityRaster{
        .texture = &replacement->candidate->texture,
        .source = {.x = frame.texture_x,
                   .y = frame.texture_y,
                   .width = frame.texture_w,
                   .height = frame.texture_h},
        .transient_replacement = true,
    };
  }
  if (!item.sprite.has_value()) return std::nullopt;
  const LoadedTexture* texture = FindTexture(assets, item.sprite->texture);
  if (texture == nullptr) {
    return absl::FailedPreconditionError("entity handle has no loaded pixels");
  }
  return EntityRaster{
      .texture = &texture->pixels,
      .source = {.x = item.sprite->source.x,
                 .y = item.sprite->source.y,
                 .width = item.sprite->source.width,
                 .height = item.sprite->source.height},
  };
}

absl::StatusOr<WorldRenderStats> CompositeWorldLayer(
    RgbaImage& canvas, const LevelReviewAssets& assets, const Level& level, const WorldLayer& layer,
    const Camera& camera, const TransientEntityReplacement* replacement) {
  WorldRenderStats stats;
  if (assets.tileset.has_value()) {
    TextureHandle atlas_handle;
    const LoadedTexture* atlas = nullptr;
    if (!assets.tileset->texture_id.empty()) {
      const auto loaded = assets.textures.find(assets.tileset->texture_id);
      if (loaded == assets.textures.end()) {
        return absl::FailedPreconditionError("tileset atlas pixels are unavailable");
      }
      atlas_handle = loaded->second.handle;
      atlas = &loaded->second;
    }
    ASSIGN_OR_RETURN(
        const TileRenderBatch batch,
        ComposeLevelTileRenderBatch(level, layer, *assets.tileset, atlas_handle, camera, {}));
    if (!batch.items.empty() && atlas == nullptr) {
      return absl::FailedPreconditionError("visible tiles require a loaded atlas");
    }
    for (const TileRenderItem& item : batch.items) {
      const Vec minimum = camera.WorldToScreen(item.bounds.min);
      const Vec maximum = camera.WorldToScreen(item.bounds.max);
      RETURN_IF_ERROR(CompositeRgbaNearest(canvas, atlas->pixels,
                                           {.x = item.source.x,
                                            .y = item.source.y,
                                            .width = item.source.width,
                                            .height = item.source.height},
                                           {.x = minimum.x,
                                            .y = minimum.y,
                                            .width = maximum.x - minimum.x,
                                            .height = maximum.y - minimum.y}));
    }
    stats.tile_count = batch.items.size();
  }

  ASSIGN_OR_RETURN(const std::vector<EntityRenderItem> items,
                   ComposeEntityRenderItems(layer.entities, assets.sprite_lookup, {}));
  for (const EntityRenderItem& item : items) {
    if (!IntersectsScreen(camera, item.bounds)) continue;
    const Vec minimum = camera.WorldToScreen(item.bounds.min);
    const Vec maximum = camera.WorldToScreen(item.bounds.max);
    ASSIGN_OR_RETURN(const std::optional<EntityRaster> raster,
                     ResolveEntityRaster(assets, item, replacement));
    if (raster.has_value()) {
      if (raster->texture == nullptr) {
        return absl::InternalError("resolved entity raster has no texture pixels");
      }
      RETURN_IF_ERROR(CompositeRgbaNearest(canvas, *raster->texture, raster->source,
                                           {.x = minimum.x,
                                            .y = minimum.y,
                                            .width = maximum.x - minimum.x,
                                            .height = maximum.y - minimum.y}));
      if (raster->transient_replacement) {
        stats.transient_replacement_entity_ids.push_back(item.entity_id);
      }
    } else {
      const int x = static_cast<int>(std::floor(minimum.x));
      const int y = static_cast<int>(std::floor(minimum.y));
      const int width = static_cast<int>(std::ceil(maximum.x)) - x;
      const int height = static_cast<int>(std::ceil(maximum.y)) - y;
      if (width > 0 && height > 0) {
        RETURN_IF_ERROR(FillRgbaRect(canvas, x, y, width, height, kPlaceholder));
      }
    }
    stats.entity_ids.push_back(item.entity_id);
  }
  return stats;
}

absl::StatusOr<RenderedLevelFrame> RenderCompleteFrame(
    const Level& level, const LevelReviewAssets& assets, const Camera& camera,
    const TransientEntityReplacement* replacement) {
  ASSIGN_OR_RETURN(const std::optional<ResolvedParallaxEnvironment> environment,
                   ResolveParallaxEnvironment(level.zones, camera.position));
  absl::StatusOr<RgbaImage> canvas =
      environment.has_value()
          ? CreateSolidRgbaImage(camera.viewport_width, camera.viewport_height, kBackground)
          : CreateCheckerboardRgbaImage(camera.viewport_width, camera.viewport_height, 16,
                                        kCheckerLight, kCheckerDark);
  if (!canvas.ok()) return canvas.status();

  RenderedLevelFrame result{.image = std::move(*canvas), .environment = environment};
  if (environment.has_value()) {
    result.render_sequence.push_back(absl::StrCat("parallax:", environment->primary.theme_id));
    if (environment->secondary.has_value() && environment->secondary_weight > 0.0) {
      result.render_sequence.push_back(absl::StrCat("parallax:", environment->secondary->theme_id));
    }
  }
  RETURN_IF_ERROR(CompositeEnvironment(result.image, assets, camera, environment));
  for (const WorldLayer& layer : level.layers) {
    ASSIGN_OR_RETURN(const WorldRenderStats layer_stats,
                     CompositeWorldLayer(result.image, assets, level, layer, camera, replacement));
    result.world.tile_count += layer_stats.tile_count;
    result.world.entity_ids.insert(result.world.entity_ids.end(), layer_stats.entity_ids.begin(),
                                   layer_stats.entity_ids.end());
    result.world.transient_replacement_entity_ids.insert(
        result.world.transient_replacement_entity_ids.end(),
        layer_stats.transient_replacement_entity_ids.begin(),
        layer_stats.transient_replacement_entity_ids.end());
    result.render_sequence.push_back(absl::StrCat("world-layer:", layer.id, ":tiles"));
    result.render_sequence.push_back(absl::StrCat("world-layer:", layer.id, ":entities"));
  }
  return result;
}

absl::StatusOr<RgbaImage> RenderParallaxPass(
    const LevelReviewAssets& assets, const Camera& camera,
    const std::optional<ResolvedParallaxEnvironment>& environment,
    std::optional<ThemeLayerSelection> selection = std::nullopt) {
  ASSIGN_OR_RETURN(RgbaImage canvas, CreateSolidRgbaImage(camera.viewport_width,
                                                          camera.viewport_height, kTransparent));
  RETURN_IF_ERROR(CompositeEnvironment(canvas, assets, camera, environment, selection));
  return canvas;
}

absl::StatusOr<RgbaImage> RenderWorldLayerPass(const Level& level, const LevelReviewAssets& assets,
                                               const WorldLayer& layer, const Camera& camera,
                                               const TransientEntityReplacement* replacement) {
  ASSIGN_OR_RETURN(RgbaImage canvas, CreateSolidRgbaImage(camera.viewport_width,
                                                          camera.viewport_height, kTransparent));
  RETURN_IF_ERROR(CompositeWorldLayer(canvas, assets, level, layer, camera, replacement).status());
  return canvas;
}

AlphaMetrics MeasureAlpha(const RgbaImage& image) {
  AlphaMetrics result{
      .min_x = image.width,
      .min_y = image.height,
      .empty_left = image.width,
      .empty_top = image.height,
      .empty_right = image.width,
      .empty_bottom = image.height,
  };
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      if (image.pixels[offset + 3] == 0) continue;
      result.occupied = true;
      result.min_x = std::min(result.min_x, x);
      result.min_y = std::min(result.min_y, y);
      result.max_x = std::max(result.max_x, x);
      result.max_y = std::max(result.max_y, y);
    }
  }
  if (!result.occupied) return result;
  result.empty_left = result.min_x;
  result.empty_top = result.min_y;
  result.empty_right = image.width - 1 - result.max_x;
  result.empty_bottom = image.height - 1 - result.max_y;
  return result;
}

nlohmann::json AlphaToJson(const AlphaMetrics& alpha) {
  if (!alpha.occupied) return {{"occupied", false}};
  return {
      {"occupied", true},
      {"bounds",
       {{"min_x", alpha.min_x},
        {"min_y", alpha.min_y},
        {"max_x", alpha.max_x},
        {"max_y", alpha.max_y}}},
      {"empty_edges",
       {{"left", alpha.empty_left},
        {"top", alpha.empty_top},
        {"right", alpha.empty_right},
        {"bottom", alpha.empty_bottom}}},
  };
}

nlohmann::json EnvironmentToJson(const std::optional<ResolvedParallaxEnvironment>& environment) {
  if (!environment.has_value()) return nullptr;
  nlohmann::json result{
      {"active_zone_id", environment->active_zone_id},
      {"primary_theme_id", environment->primary.theme_id},
      {"secondary_weight", environment->secondary_weight},
  };
  if (environment->secondary.has_value()) {
    result["secondary_theme_id"] = environment->secondary->theme_id;
  }
  return result;
}

nlohmann::json BaseArtifactMetadata(const LevelReviewRoute& route,
                                    const LevelReviewCameraSample& sample,
                                    const std::optional<ResolvedParallaxEnvironment>& environment) {
  return {
      {"route_id", route.id},
      {"zone_id", route.zone_id},
      {"track_index", route.track_index},
      {"sample_id", sample.id},
      {"progress", sample.progress},
      {"key_roles", sample.key_roles},
      {"camera",
       {{"center", VecToJson(sample.camera.position)},
        {"zoom", sample.camera.zoom},
        {"visible_world", VisibleBoundsToJson(sample.camera)}}},
      {"environment", EnvironmentToJson(environment)},
  };
}

absl::StatusOr<ContactSheetAccumulator> CreateContactSheet(size_t frame_count,
                                                           const GameViewSize& game_view) {
  if (frame_count == 0) {
    return absl::InvalidArgumentError("contact sheet requires at least one frame");
  }
  const int columns = std::min(kContactColumns, static_cast<int>(frame_count));
  const int rows = static_cast<int>((frame_count + columns - 1) / columns);
  const int thumbnail_height =
      std::max(1, static_cast<int>(std::lround(static_cast<double>(kContactWidth) *
                                               game_view.height / game_view.width)));
  const int width = columns * kContactWidth + (columns + 1) * kContactGutter;
  const int height = rows * thumbnail_height + (rows + 1) * kContactGutter;
  ASSIGN_OR_RETURN(RgbaImage image, CreateSolidRgbaImage(width, height, kCheckerDark));
  return ContactSheetAccumulator{
      .image = std::move(image),
      .frame_count = frame_count,
      .columns = columns,
      .thumbnail_height = thumbnail_height,
  };
}

absl::Status AddContactSheetFrame(ContactSheetAccumulator& sheet, const RgbaImage& source,
                                  const std::string& artifact_id) {
  if (!source.IsValid() || artifact_id.empty() || sheet.next_frame >= sheet.frame_count) {
    return absl::FailedPreconditionError("contact sheet received an invalid or excess frame");
  }
  const int column = static_cast<int>(sheet.next_frame) % sheet.columns;
  const int row = static_cast<int>(sheet.next_frame) / sheet.columns;
  RETURN_IF_ERROR(CompositeRgbaNearest(
      sheet.image, source, {.x = 0, .y = 0, .width = source.width, .height = source.height},
      {.x = static_cast<double>(kContactGutter + column * (kContactWidth + kContactGutter)),
       .y = static_cast<double>(kContactGutter + row * (sheet.thumbnail_height + kContactGutter)),
       .width = static_cast<double>(kContactWidth),
       .height = static_cast<double>(sheet.thumbnail_height)}));
  sheet.ordered_artifact_ids.push_back(artifact_id);
  ++sheet.next_frame;
  return absl::OkStatus();
}

int MapCoordinate(double value, double world_extent, int image_extent) {
  return std::clamp(static_cast<int>(std::lround(value * image_extent / world_extent)), 0,
                    image_extent - 1);
}

absl::StatusOr<nlohmann::json> AnnotateFocusedEntity(RgbaImage& image, const Camera& camera,
                                                     const FocusedEntityContext& focus) {
  const Vec minimum = camera.WorldToScreen(focus.bounds.min);
  const Vec maximum = camera.WorldToScreen(focus.bounds.max);
  const Vec anchor = camera.WorldToScreen(focus.position);
  const int min_x = static_cast<int>(std::floor(minimum.x));
  const int min_y = static_cast<int>(std::floor(minimum.y));
  const int max_x = static_cast<int>(std::ceil(maximum.x)) - 1;
  const int max_y = static_cast<int>(std::ceil(maximum.y)) - 1;
  RETURN_IF_ERROR(DrawRgbaOutline(image, min_x, min_y, max_x, max_y, kFocus));
  RETURN_IF_ERROR(DrawRgbaCross(image, static_cast<int>(std::lround(anchor.x)),
                                static_cast<int>(std::lround(anchor.y)), 5, kFocus));
  return nlohmann::json{
      {"bounds", {{"min", VecToJson(minimum)}, {"max", VecToJson(maximum)}}},
      {"anchor", VecToJson(anchor)},
      {"intersects_viewport", maximum.x > 0.0 && maximum.y > 0.0 &&
                                  minimum.x < camera.viewport_width &&
                                  minimum.y < camera.viewport_height},
      {"color", "#ffdc28"},
  };
}

absl::StatusOr<RgbaImage> RenderLayoutMap(const Level& level, const LevelReviewAssets& assets,
                                          const std::vector<LevelReviewRoute>& routes,
                                          const std::optional<FocusedEntityContext>& focus) {
  const double scale = std::min(static_cast<double>(kLayoutMaximumWidth) / level.width,
                                static_cast<double>(kLayoutMaximumHeight) / level.height);
  const int width = std::max(1, static_cast<int>(std::ceil(level.width * scale)));
  const int height = std::max(1, static_cast<int>(std::ceil(level.height * scale)));
  ASSIGN_OR_RETURN(RgbaImage image, CreateSolidRgbaImage(width, height, kBackground));

  for (const ParallaxZone& zone : level.zones) {
    RETURN_IF_ERROR(DrawRgbaOutline(image, MapCoordinate(zone.min_point.x, level.width, width),
                                    MapCoordinate(zone.min_point.y, level.height, height),
                                    MapCoordinate(zone.max_point.x, level.width, width),
                                    MapCoordinate(zone.max_point.y, level.height, height),
                                    kZoneOutline));
  }
  for (const LevelReviewRoute& route : routes) {
    auto zoom = std::find(kReviewZooms.begin(), kReviewZooms.end(), route.zoom);
    if (zoom == kReviewZooms.end()) {
      return absl::InternalError("layout map received an unsupported review zoom");
    }
    const RgbaColor8 color = kRouteColors[static_cast<size_t>(zoom - kReviewZooms.begin())];
    const int min_x = MapCoordinate(route.centers.min.x, level.width, width);
    const int min_y = MapCoordinate(route.centers.min.y, level.height, height);
    const int max_x = MapCoordinate(route.centers.max.x, level.width, width);
    const int max_y = MapCoordinate(route.centers.max.y, level.height, height);
    if (route.horizontal) {
      RETURN_IF_ERROR(FillRgbaRect(image, min_x, min_y, std::max(1, max_x - min_x + 1), 2, color));
    } else {
      RETURN_IF_ERROR(FillRgbaRect(image, min_x, min_y, 2, std::max(1, max_y - min_y + 1), color));
    }
  }
  for (size_t layer_index = 0; layer_index < level.layers.size(); ++layer_index) {
    const RgbaColor8 color = kLayerColors[layer_index % kLayerColors.size()];
    for (const auto& [unused, entity] : level.layers[layer_index].entities) {
      static_cast<void>(unused);
      const ResolvedSprite resolved = FindSprite(assets.sprite_lookup, entity.sprite_id);
      ASSIGN_OR_RETURN(const WorldRect bounds, CalculateEntityBounds(entity, resolved.sprite));
      RETURN_IF_ERROR(DrawRgbaOutline(image, MapCoordinate(bounds.min.x, level.width, width),
                                      MapCoordinate(bounds.min.y, level.height, height),
                                      MapCoordinate(bounds.max.x, level.width, width),
                                      MapCoordinate(bounds.max.y, level.height, height), color));
      RETURN_IF_ERROR(DrawRgbaCross(
          image, MapCoordinate(entity.transform.position.x, level.width, width),
          MapCoordinate(entity.transform.position.y, level.height, height), 2, color));
    }
  }
  RETURN_IF_ERROR(DrawRgbaCross(image, MapCoordinate(level.spawn_point.x, level.width, width),
                                MapCoordinate(level.spawn_point.y, level.height, height), 4,
                                kSpawn));
  if (focus.has_value()) {
    RETURN_IF_ERROR(DrawRgbaOutline(image, MapCoordinate(focus->bounds.min.x, level.width, width),
                                    MapCoordinate(focus->bounds.min.y, level.height, height),
                                    MapCoordinate(focus->bounds.max.x, level.width, width),
                                    MapCoordinate(focus->bounds.max.y, level.height, height),
                                    kFocus));
    RETURN_IF_ERROR(DrawRgbaCross(image, MapCoordinate(focus->position.x, level.width, width),
                                  MapCoordinate(focus->position.y, level.height, height), 4,
                                  kFocus));
  }
  return image;
}

absl::Status AddCoverageFindings(CurationReview& review, const Level& level,
                                 const LevelReviewAssets& assets, const GameViewSize& game_view) {
  for (const ParallaxZone& zone : level.zones) {
    const ParallaxTheme& theme = assets.themes.at(zone.theme_id);
    for (const ParallaxLayer& layer : theme.layers) {
      std::vector<ParallaxElementSize> sizes;
      sizes.reserve(layer.elements.size());
      for (const ParallaxElement& element : layer.elements) {
        const RgbaImage& image = assets.textures.at(element.texture_id).pixels;
        sizes.push_back({.element_id = element.id, .width = image.width, .height = image.height});
      }
      ASSIGN_OR_RETURN(const WorldRect bounds, CalculateParallaxCompositionBounds(layer, sizes));
      ASSIGN_OR_RETURN(
          const CameraCoverageDiagnostics coverage,
          AnalyzeCameraCoverage(
              layer, bounds, zone.min_point, zone.max_point, game_view, kParallaxAuthoringZoomRange,
              CameraWorldBounds{.min = {0, 0}, .max = {level.width, level.height}}));
      if (coverage.horizontal.covers() && coverage.vertical.covers()) continue;
      review.findings.push_back({
          .severity = CurationFindingSeverity::kWarning,
          .code = "camera-coverage-gap",
          .subject = absl::StrCat(level.name, " / ", zone.name, " / ", layer.name),
          .message = "finite parallax geometry does not cover the complete supported camera route",
      });
    }
  }
  return absl::OkStatus();
}

nlohmann::json AddEntityEvidence(CurationReview& review, const Level& level) {
  nlohmann::json layers = nlohmann::json::array();
  for (const WorldLayer& layer : level.layers) {
    std::vector<double> anchors_x;
    std::vector<double> anchors_y;
    for (const auto& [unused, entity] : layer.entities) {
      static_cast<void>(unused);
      if (!entity.active) continue;
      anchors_x.push_back(entity.transform.position.x);
      anchors_y.push_back(entity.transform.position.y);
    }
    std::sort(anchors_x.begin(), anchors_x.end());
    std::sort(anchors_y.begin(), anchors_y.end());
    size_t painted_tile_count = 0;
    for (const auto& [unused, chunk] : layer.tile_chunks) {
      static_cast<void>(unused);
      painted_tile_count += static_cast<size_t>(std::count_if(
          chunk.tiles.begin(), chunk.tiles.end(), [](int tile_id) { return tile_id != 0; }));
    }
    double largest_gap = level.width;
    if (!anchors_x.empty()) {
      largest_gap = anchors_x.front();
      for (size_t index = 1; index < anchors_x.size(); ++index) {
        largest_gap = std::max(largest_gap, anchors_x[index] - anchors_x[index - 1]);
      }
      largest_gap = std::max(largest_gap, level.width - anchors_x.back());
    }
    layers.push_back({
        {"id", layer.id},
        {"name", layer.name},
        {"active_entity_count", anchors_x.size()},
        {"painted_tile_count", painted_tile_count},
        {"occupied_x", anchors_x.empty()
                           ? nlohmann::json(nullptr)
                           : nlohmann::json{{"min", anchors_x.front()}, {"max", anchors_x.back()}}},
        {"occupied_y", anchors_y.empty()
                           ? nlohmann::json(nullptr)
                           : nlohmann::json{{"min", anchors_y.front()}, {"max", anchors_y.back()}}},
        {"largest_empty_horizontal_interval", largest_gap},
    });
    if (anchors_x.empty() && painted_tile_count == 0) {
      review.findings.push_back({
          .severity = CurationFindingSeverity::kInfo,
          .code = "empty-world-layer",
          .subject = absl::StrCat(level.name, " / ", layer.name),
          .message = "world layer contains no active entities or painted tiles",
      });
    }
  }
  return layers;
}

absl::Status PreflightArtifactPixels(const std::vector<LevelReviewRoute>& routes,
                                     const LevelReviewAssets& assets, const Level& level,
                                     const GameViewSize& game_view, bool focused) {
  int64_t native_frames = 0;
  int64_t auxiliary_pixels = 0;
  for (const LevelReviewRoute& route : routes) {
    native_frames += route.samples.size();
    if (focused) native_frames += route.samples.size();
    const int columns = std::min(kContactColumns, static_cast<int>(route.samples.size()));
    const int rows = static_cast<int>((route.samples.size() + columns - 1) / columns);
    const int thumbnail_height =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(kContactWidth) *
                                                 game_view.height / game_view.width)));
    auxiliary_pixels +=
        static_cast<int64_t>(columns * kContactWidth + (columns + 1) * kContactGutter) *
        (rows * thumbnail_height + (rows + 1) * kContactGutter);
    for (const LevelReviewCameraSample& sample : route.samples) {
      if (sample.key_roles.empty()) continue;
      native_frames += 1 + level.layers.size();
      ASSIGN_OR_RETURN(const std::optional<ResolvedParallaxEnvironment> environment,
                       ResolveParallaxEnvironment(level.zones, sample.camera.position));
      if (!environment.has_value()) continue;
      native_frames += assets.themes.at(environment->primary.theme_id).layers.size();
      if (environment->secondary.has_value()) {
        native_frames += assets.themes.at(environment->secondary->theme_id).layers.size();
      }
    }
  }
  const int64_t native_pixels =
      native_frames * static_cast<int64_t>(game_view.width) * game_view.height;
  const double layout_scale = std::min(static_cast<double>(kLayoutMaximumWidth) / level.width,
                                       static_cast<double>(kLayoutMaximumHeight) / level.height);
  auxiliary_pixels +=
      static_cast<int64_t>(std::max(1, static_cast<int>(std::ceil(level.width * layout_scale)))) *
      std::max(1, static_cast<int>(std::ceil(level.height * layout_scale)));
  if (native_pixels + auxiliary_pixels > kMaximumCurationReviewPixels) {
    return absl::ResourceExhaustedError(
        "planned level review artifacts exceed the curation pixel budget");
  }
  return absl::OkStatus();
}

absl::StatusOr<CurationReview> BuildReview(Api& api, const Level& level,
                                           const CurationReviewRequest& request,
                                           const PreparedPropCandidate* candidate,
                                           CurationArtifactSink& artifact_sink) {
  const GameViewSize game_view = api.GetConfig()->game_view;
  if (!game_view.IsValid()) return absl::FailedPreconditionError("game view is invalid");
  if (candidate != nullptr && !request.focus_entity_id.has_value()) {
    return absl::InvalidArgumentError(
        "transient prop candidate review requires a focused replacement entity");
  }
  std::optional<FocusedEntityContext> focus;
  if (request.focus_entity_id.has_value()) {
    ASSIGN_OR_RETURN(focus, FindFocusedEntity(level, *request.focus_entity_id));
  }
  absl::StatusOr<std::vector<LevelReviewRoute>> planned_routes =
      focus.has_value()
          ? PlanFocusedLevelReviewRoutes(level, game_view, focus->id, focus->position, kReviewZooms)
          : PlanLevelReviewRoutes(level, game_view, kReviewZooms);
  ASSIGN_OR_RETURN(const std::vector<LevelReviewRoute> routes, std::move(planned_routes));
  ASSIGN_OR_RETURN(LevelReviewAssets assets, LoadAssets(api, level));

  std::optional<Level> preview_level;
  std::optional<TransientEntityReplacement> replacement;
  const Level* rendered_level = &level;
  if (candidate != nullptr) {
    preview_level = level;
    ASSIGN_OR_RETURN(
        replacement,
        ApplyTransientPropCandidate(*preview_level, assets, *request.focus_entity_id, *candidate));
    rendered_level = &*preview_level;
    ASSIGN_OR_RETURN(focus, FindFocusedEntity(*rendered_level, *request.focus_entity_id));
  }
  if (focus.has_value()) RETURN_IF_ERROR(ResolveFocusedEntityBounds(assets, *focus));
  const TransientEntityReplacement* replacement_ptr =
      replacement.has_value() ? &*replacement : nullptr;

  CurationReview review{
      .kind = "level",
      .asset_id = level.id,
      .asset_name = level.name,
      .metadata =
          {
              {"definition", LevelToJson(level)},
              {"game_view", {{"width", game_view.width}, {"height", game_view.height}}},
              {"zooms", kReviewZooms},
          },
  };
  if (focus.has_value()) {
    review.metadata["review_mode"] = "focused-entity";
    review.metadata["focus"] = FocusToJson(*focus);
  }
  if (candidate != nullptr && replacement.has_value() && focus.has_value()) {
    review.metadata["transient_prop_candidate"] = {
        {"operation", PropCandidateOperationId(candidate->operation)},
        {"asset_id", candidate->recipe.id},
        {"asset_name", candidate->recipe.name},
        {"source_rgba_sha256", candidate->source_content_digest},
        {"final_rgba_sha256", candidate->recipe.final_pixel_digest},
        {"candidate_matches_deterministic_output", candidate->matches_deterministic_output},
        {"candidate_sprite_id", candidate->sprite.id},
        {"placement_mode", BlueprintPlacementModeId(candidate->placement_mode)},
        {"requested_candidate", candidate->requested_candidate},
        {"target",
         {{"entity_id", focus->id},
          {"world_layer_id", focus->layer_id},
          {"world_layer_name", focus->layer_name},
          {"position", VecToJson(focus->position)},
          {"persisted_sprite_id", replacement->persisted_sprite_id}}},
        {"workspace_mutated", false},
    };
    if (!candidate->matches_deterministic_output) {
      review.findings.push_back({
          .severity = CurationFindingSeverity::kWarning,
          .code = "candidate-recipe-mismatch",
          .subject = candidate->recipe.name,
          .message = "the requested recipe does not exactly describe the deterministic output; "
                     "commit will refuse it",
      });
    }
  }
  RETURN_IF_ERROR(
      PreflightArtifactPixels(routes, assets, *rendered_level, game_view, focus.has_value()));
  review.metadata["world_layers"] = AddEntityEvidence(review, level);
  RETURN_IF_ERROR(AddCoverageFindings(review, level, assets, game_view));

  std::map<std::string, int> missing_environment_counts;
  std::map<std::string, int> maximum_empty_bottom;
  size_t total_samples = 0;
  for (const LevelReviewRoute& route : routes) {
    const std::string zoom_id = ZoomId(route.zoom);
    ASSIGN_OR_RETURN(ContactSheetAccumulator contact,
                     CreateContactSheet(route.samples.size(), game_view));
    for (const LevelReviewCameraSample& sample : route.samples) {
      ASSIGN_OR_RETURN(
          RenderedLevelFrame rendered,
          RenderCompleteFrame(*rendered_level, assets, sample.camera, replacement_ptr));
      nlohmann::json metadata = BaseArtifactMetadata(route, sample, rendered.environment);
      metadata["view"] = "complete";
      metadata["visible_tile_count"] = rendered.world.tile_count;
      metadata["visible_entity_ids"] = rendered.world.entity_ids;
      if (!rendered.world.transient_replacement_entity_ids.empty()) {
        metadata["transient_replacement_entity_ids"] =
            rendered.world.transient_replacement_entity_ids;
      }
      metadata["render_sequence"] = rendered.render_sequence;
      const std::string artifact_id =
          absl::StrCat("complete-", zoom_id, "-", route.id, "-", sample.id);
      std::optional<CurationArtifact> focused_artifact;
      if (focus.has_value()) {
        RgbaImage focused_image = rendered.image;
        ASSIGN_OR_RETURN(nlohmann::json screen_evidence,
                         AnnotateFocusedEntity(focused_image, sample.camera, *focus));
        nlohmann::json focused_metadata = metadata;
        focused_metadata["view"] = "focused-entity";
        focused_metadata["focus"] = FocusToJson(*focus);
        focused_metadata["focus"]["screen"] = std::move(screen_evidence);
        const std::string focused_id =
            absl::StrCat("focused-entity-", zoom_id, "-", route.id, "-", sample.id);
        RETURN_IF_ERROR(AddContactSheetFrame(contact, focused_image, focused_id));
        focused_artifact = CurationArtifact{
            .id = focused_id,
            .relative_path = absl::StrCat("frames/focused-entity/", zoom_id, "/", route.id, "/",
                                          sample.id, ".png"),
            .description = absl::StrCat("Focused entity ", focus->id, " at zoom ", route.zoom),
            .image = std::move(focused_image),
            .metadata = std::move(focused_metadata),
        };
      } else {
        RETURN_IF_ERROR(AddContactSheetFrame(contact, rendered.image, artifact_id));
      }
      RETURN_IF_ERROR(artifact_sink.Add({
          .id = artifact_id,
          .relative_path =
              absl::StrCat("frames/complete/", zoom_id, "/", route.id, "/", sample.id, ".png"),
          .description = absl::StrCat("Complete level at ", route.zone_name, ", zoom ", route.zoom,
                                      ", progress ", sample.progress),
          .image = std::move(rendered.image),
          .metadata = std::move(metadata),
      }));
      if (focused_artifact.has_value()) {
        RETURN_IF_ERROR(artifact_sink.Add(*focused_artifact));
      }
      ++total_samples;
      if (!rendered.environment.has_value()) ++missing_environment_counts[route.id];

      if (sample.key_roles.empty()) continue;
      const std::string pass_root =
          absl::StrCat("passes/", zoom_id, "/", route.id, "/", sample.id, "/");
      ASSIGN_OR_RETURN(RgbaImage parallax,
                       RenderParallaxPass(assets, sample.camera, rendered.environment));
      nlohmann::json parallax_metadata = BaseArtifactMetadata(route, sample, rendered.environment);
      parallax_metadata["view"] = "parallax";
      parallax_metadata["alpha"] = AlphaToJson(MeasureAlpha(parallax));
      RETURN_IF_ERROR(artifact_sink.Add({
          .id = absl::StrCat("parallax-", zoom_id, "-", route.id, "-", sample.id),
          .relative_path = absl::StrCat(pass_root, "parallax.png"),
          .description = "Resolved parallax environment without world layers",
          .image = std::move(parallax),
          .metadata = std::move(parallax_metadata),
      }));

      std::vector<std::string> active_theme_ids;
      if (rendered.environment.has_value()) {
        active_theme_ids.push_back(rendered.environment->primary.theme_id);
        if (rendered.environment->secondary.has_value()) {
          active_theme_ids.push_back(rendered.environment->secondary->theme_id);
        }
      }
      for (const std::string& theme_id : active_theme_ids) {
        const ParallaxTheme& theme = assets.themes.at(theme_id);
        for (int layer_index = 0; layer_index < static_cast<int>(theme.layers.size());
             ++layer_index) {
          ASSIGN_OR_RETURN(RgbaImage layer_image,
                           RenderParallaxPass(assets, sample.camera, rendered.environment,
                                              ThemeLayerSelection{.theme_id = theme_id,
                                                                  .layer_index = layer_index}));
          const AlphaMetrics alpha = MeasureAlpha(layer_image);
          nlohmann::json layer_metadata = BaseArtifactMetadata(route, sample, rendered.environment);
          layer_metadata["view"] = "parallax-layer";
          layer_metadata["theme_id"] = theme_id;
          layer_metadata["layer_index"] = layer_index;
          layer_metadata["layer_name"] = theme.layers[layer_index].name;
          layer_metadata["alpha"] = AlphaToJson(alpha);
          const std::string layer_key = absl::StrCat(theme_id, "/", layer_index);
          if (!focus.has_value() && std::abs(route.zoom - kReviewZooms.front()) < 1e-9) {
            maximum_empty_bottom[layer_key] =
                std::max(maximum_empty_bottom[layer_key], alpha.empty_bottom);
          }
          RETURN_IF_ERROR(artifact_sink.Add({
              .id = absl::StrCat("parallax-layer-", zoom_id, "-", route.id, "-", sample.id, "-",
                                 theme_id, "-", layer_index),
              .relative_path =
                  absl::StrCat(pass_root, "parallax-", theme_id, "-layer-", layer_index, ".png"),
              .description =
                  absl::StrCat("Isolated parallax layer ", theme.layers[layer_index].name),
              .image = std::move(layer_image),
              .metadata = std::move(layer_metadata),
          }));
        }
      }

      for (const WorldLayer& layer : rendered_level->layers) {
        ASSIGN_OR_RETURN(
            RgbaImage layer_image,
            RenderWorldLayerPass(*rendered_level, assets, layer, sample.camera, replacement_ptr));
        nlohmann::json layer_metadata = BaseArtifactMetadata(route, sample, rendered.environment);
        layer_metadata["view"] = "world-layer";
        layer_metadata["world_layer_id"] = layer.id;
        layer_metadata["world_layer_name"] = layer.name;
        layer_metadata["alpha"] = AlphaToJson(MeasureAlpha(layer_image));
        RETURN_IF_ERROR(artifact_sink.Add({
            .id =
                absl::StrCat("world-layer-", zoom_id, "-", route.id, "-", sample.id, "-", layer.id),
            .relative_path = absl::StrCat(pass_root, "world-layer-", layer.id, ".png"),
            .description = absl::StrCat("Isolated world layer ", layer.name),
            .image = std::move(layer_image),
            .metadata = std::move(layer_metadata),
        }));
      }
    }
    if (contact.next_frame != contact.frame_count) {
      return absl::InternalError("contact sheet did not receive every planned route frame");
    }
    const std::string contact_id = absl::StrCat(zoom_id, "-", route.id);
    nlohmann::json contact_metadata = {
        {"view", "contact-sheet"},
        {"route_id", route.id},
        {"track_index", route.track_index},
        {"zoom", route.zoom},
        {"columns", contact.columns},
        {"ordered_artifact_ids", std::move(contact.ordered_artifact_ids)},
    };
    if (focus.has_value()) contact_metadata["focus_entity_id"] = focus->id;
    const std::string contact_mode = focus.has_value() ? "focused-entity" : "complete";
    RETURN_IF_ERROR(artifact_sink.Add({
        .id = absl::StrCat("contact-sheet-", contact_id),
        .relative_path = absl::StrCat("contact-sheets/", contact_mode, "-", contact_id, ".png"),
        .description = absl::StrCat(focus.has_value() ? "Focused" : "Complete",
                                    " contact sheet for ", route.id, " at zoom ", route.zoom),
        .image = std::move(contact.image),
        .metadata = std::move(contact_metadata),
    }));
  }

  ASSIGN_OR_RETURN(RgbaImage layout, RenderLayoutMap(*rendered_level, assets, routes, focus));
  nlohmann::json layout_metadata = {
      {"view", "layout-map"},
      {"zone_color", "#e6b928"},
      {"route_colors", {{"z050", "#46d2ff"}, {"z100", "#d278ff"}, {"z200", "#ffa050"}}},
      {"spawn_color", "#50ff78"},
  };
  if (focus.has_value()) {
    layout_metadata["focus_entity_id"] = focus->id;
    layout_metadata["focus_color"] = "#ffdc28";
  }
  RETURN_IF_ERROR(artifact_sink.Add({
      .id = "layout-map",
      .relative_path = "layout-map.png",
      .description = "Level bounds, zones, review routes, spawn, and entity placement overview",
      .image = std::move(layout),
      .metadata = std::move(layout_metadata),
  }));

  for (const auto& [route_id, count] : missing_environment_counts) {
    review.findings.push_back({
        .severity = CurationFindingSeverity::kWarning,
        .code = "route-without-environment",
        .subject = absl::StrCat(level.name, " / ", route_id),
        .message = absl::StrCat(count, " sampled camera frames resolve no parallax environment"),
    });
  }
  for (const auto& [layer_key, empty_bottom] : maximum_empty_bottom) {
    if (empty_bottom <= 0) continue;
    review.findings.push_back({
        .severity = CurationFindingSeverity::kInfo,
        .code = "parallax-layer-empty-bottom",
        .subject = layer_key,
        .message = absl::StrCat("isolated minimum-zoom evidence leaves up to ", empty_bottom,
                                " transparent screen rows below this layer"),
    });
  }
  review.metadata["route_count"] = routes.size();
  review.metadata["sample_count"] = total_samples;
  return review;
}

absl::StatusOr<Level*> ResolveReviewLevel(Api& api, const CurationReviewRequest& request) {
  ASSIGN_OR_RETURN(Level * level, api.GetLevel(request.asset_id));
  if (level == nullptr || level->id != request.asset_id) {
    return absl::FailedPreconditionError("level lookup returned invalid data");
  }
  RETURN_IF_ERROR(ValidateLevel(*level));
  return level;
}

absl::StatusOr<PreparedPropCandidate> PrepareTransientPropCandidate(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate) {
  if (!request.focus_entity_id.has_value()) {
    return absl::InvalidArgumentError(
        "transient prop candidate review requires a focused replacement entity");
  }
  return PreparePropCandidateForReview(api, request.candidate_root, std::nullopt, candidate);
}

}  // namespace

absl::StatusOr<CurationReview> LevelReviewer::Review(Api& api,
                                                     const CurationReviewRequest& request) const {
  ASSIGN_OR_RETURN(Level * level, ResolveReviewLevel(api, request));
  CollectingArtifactSink artifact_sink;
  ASSIGN_OR_RETURN(CurationReview review,
                   BuildReview(api, *level, request, nullptr, artifact_sink));
  review.artifacts = std::move(artifact_sink.artifacts);
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

absl::StatusOr<size_t> LevelReviewer::PublishReview(Api& api, const CurationReviewRequest& request,
                                                    const std::string& output_path) const {
  ASSIGN_OR_RETURN(Level * level, ResolveReviewLevel(api, request));
  return PublishCurationReviewStreamed(
      output_path,
      [&api, level, request](CurationArtifactSink& artifact_sink,
                             CurationReview& review) -> absl::Status {
        ASSIGN_OR_RETURN(review, BuildReview(api, *level, request, nullptr, artifact_sink));
        return absl::OkStatus();
      });
}

absl::StatusOr<CurationReview> LevelReviewer::ReviewCandidate(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate_json) const {
  ASSIGN_OR_RETURN(Level * level, ResolveReviewLevel(api, request));
  ASSIGN_OR_RETURN(const PreparedPropCandidate candidate,
                   PrepareTransientPropCandidate(api, request, candidate_json));
  CollectingArtifactSink artifact_sink;
  ASSIGN_OR_RETURN(CurationReview review,
                   BuildReview(api, *level, request, &candidate, artifact_sink));
  review.artifacts = std::move(artifact_sink.artifacts);
  RETURN_IF_ERROR(ValidateCurationReview(review));
  return review;
}

absl::StatusOr<size_t> LevelReviewer::PublishCandidateReview(Api& api,
                                                             const CurationReviewRequest& request,
                                                             const nlohmann::json& candidate_json,
                                                             const std::string& output_path) const {
  ASSIGN_OR_RETURN(Level * level, ResolveReviewLevel(api, request));
  ASSIGN_OR_RETURN(PreparedPropCandidate candidate,
                   PrepareTransientPropCandidate(api, request, candidate_json));
  return PublishCurationReviewStreamed(
      output_path,
      [&api, level, request, candidate = std::move(candidate)](
          CurationArtifactSink& artifact_sink, CurationReview& review) -> absl::Status {
        ASSIGN_OR_RETURN(review, BuildReview(api, *level, request, &candidate, artifact_sink));
        return absl::OkStatus();
      });
}

}  // namespace zebes
