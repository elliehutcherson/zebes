#include "editor/level_editor/viewport_renderer.h"

#include <cmath>
#include <cstdint>
#include <map>

#include "SDL_error.h"
#include "SDL_render.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "editor/canvas/tile_draw.h"
#include "imgui.h"
#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {
namespace {

struct NativeTextureInfo {
  int width = 0;
  int height = 0;
};

absl::Status ValidateSourceRect(const PixelRect& source, const NativeTextureInfo& texture) {
  if (!source.IsValid()) {
    return absl::InvalidArgumentError("texture source rectangle is invalid");
  }
  if (source.width > texture.width || source.height > texture.height ||
      source.x > texture.width - source.width || source.y > texture.height - source.height) {
    return absl::InvalidArgumentError("texture source rectangle exceeds its texture bounds");
  }
  return absl::OkStatus();
}

absl::StatusOr<NativeTextureInfo> QueryTextureInfo(SDL_Texture* texture) {
  NativeTextureInfo info;
  if (texture == nullptr) {
    return absl::FailedPreconditionError("texture handle cannot be resolved");
  }
  if (SDL_QueryTexture(texture, nullptr, nullptr, &info.width, &info.height) != 0 ||
      info.width <= 0 || info.height <= 0) {
    return absl::InternalError(absl::StrCat("failed to query texture: ", SDL_GetError()));
  }
  return info;
}

}  // namespace

absl::Status ViewportRenderer::RenderEntities(
    std::span<const EntityRenderItem> items) const {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (draw_list == nullptr) {
    return absl::FailedPreconditionError("viewport canvas has no active draw list");
  }

  std::map<SDL_Texture*, NativeTextureInfo> texture_info;
  for (const EntityRenderItem& item : items) {
    if (!item.bounds.IsValid()) {
      return absl::InvalidArgumentError("entity render item has invalid world bounds");
    }

    const ImVec2 screen_min = canvas_.WorldToScreen(item.bounds.min);
    const ImVec2 screen_max = canvas_.WorldToScreen(item.bounds.max);
    if (item.sprite.has_value()) {
      SDL_Texture* native_texture = SdlTextureHandleAdapter::ToNative(item.sprite->texture);
      if (native_texture == nullptr) {
        return absl::FailedPreconditionError("entity texture handle cannot be resolved");
      }

      auto [texture_it, inserted] = texture_info.try_emplace(native_texture);
      if (inserted) {
        absl::StatusOr<NativeTextureInfo> queried = QueryTextureInfo(native_texture);
        if (!queried.ok()) return queried.status();
        texture_it->second = *queried;
      }

      const NativeTextureInfo& texture = texture_it->second;
      absl::Status source_status = ValidateSourceRect(item.sprite->source, texture);
      if (!source_status.ok()) return source_status;

      const PixelRect& source = item.sprite->source;
      const ImVec2 uv_min(static_cast<float>(source.x) / texture.width,
                          static_cast<float>(source.y) / texture.height);
      const ImVec2 uv_max(static_cast<float>(source.x + source.width) / texture.width,
                          static_cast<float>(source.y + source.height) / texture.height);
      draw_list->AddImage(reinterpret_cast<ImTextureID>(native_texture), screen_min, screen_max,
                          uv_min, uv_max);
    } else {
      draw_list->AddRectFilled(screen_min, screen_max, IM_COL32(100, 100, 200, 180));
    }

    if (item.overlay_opacity > 0.0f) {
      draw_list->AddRectFilled(
          screen_min, screen_max,
          IM_COL32(255, 200, 0, static_cast<uint8_t>(item.overlay_opacity * 255.0f)));
    }
    if (item.show_border) {
      draw_list->AddRect(screen_min, screen_max, IM_COL32(255, 255, 255, 180), 0.0f, 0, 1.0f);
    }
    if (item.selected) {
      draw_list->AddRect(screen_min, screen_max, IM_COL32(255, 200, 0, 255), 0.0f, 0, 2.0f);
    }
  }
  return absl::OkStatus();
}

absl::Status ViewportRenderer::RenderTiles(const TileRenderBatch& batch) const {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (draw_list == nullptr) {
    return absl::FailedPreconditionError("viewport canvas has no active draw list");
  }
  if (!std::isfinite(batch.overlay_opacity) || batch.overlay_opacity < 0.0f ||
      batch.overlay_opacity > 1.0f) {
    return absl::InvalidArgumentError("tile overlay opacity must be between zero and one");
  }

  SDL_Texture* native_texture = nullptr;
  NativeTextureInfo texture_info;
  if (batch.atlas_texture) {
    native_texture = SdlTextureHandleAdapter::ToNative(batch.atlas_texture);
    absl::StatusOr<NativeTextureInfo> queried = QueryTextureInfo(native_texture);
    if (!queried.ok()) return queried.status();
    texture_info = *queried;
  }

  for (const TileRenderItem& item : batch.items) {
    if (item.tile_id <= 0 || !item.bounds.IsValid() || !item.source.IsValid()) {
      return absl::InvalidArgumentError("tile render item has invalid geometry");
    }

    const ImVec2 screen_min = canvas_.WorldToScreen(item.bounds.min);
    const ImVec2 screen_max = canvas_.WorldToScreen(item.bounds.max);
    if (native_texture != nullptr) {
      absl::Status source_status = ValidateSourceRect(item.source, texture_info);
      if (!source_status.ok()) return source_status;

      const ImVec2 uv_min(static_cast<float>(item.source.x) / texture_info.width,
                          static_cast<float>(item.source.y) / texture_info.height);
      const ImVec2 uv_max(
          static_cast<float>(item.source.x + item.source.width) / texture_info.width,
          static_cast<float>(item.source.y + item.source.height) / texture_info.height);
      const ImU32 tint = batch.mode == TileRenderMode::kPlacementGhost
                             ? IM_COL32(255, 255, 255, 160)
                             : IM_COL32_WHITE;
      draw_list->AddImage(reinterpret_cast<ImTextureID>(native_texture), screen_min, screen_max,
                          uv_min, uv_max, tint);
    } else if (batch.mode == TileRenderMode::kPlacementGhost) {
      draw_list->AddRectFilled(screen_min, screen_max, IM_COL32(100, 200, 100, 100));
    }

    if (batch.mode == TileRenderMode::kPlacementGhost) {
      draw_list->AddRect(screen_min, screen_max, IM_COL32(100, 200, 255, 200), 0.0f, 0, 2.0f);
      continue;
    }
    if (batch.overlay_opacity > 0.0f) {
      draw_list->AddRectFilled(
          screen_min, screen_max,
          IM_COL32(50, 100, 255, static_cast<uint8_t>(batch.overlay_opacity * 255.0f)));
    }
    if (batch.show_frame) {
      draw_list->AddRect(screen_min, screen_max, IM_COL32(200, 200, 200, 100), 0.0f, 0, 1.0f);
    }
    if (batch.show_collision && item.collision_shape != TileShape::kNone) {
      DrawShapeOverlay(draw_list, screen_min, screen_max, item.collision_shape);
    }
  }
  return absl::OkStatus();
}

void ViewportRenderer::RenderZoneGizmos(std::span<const ZoneGizmoItem> items) const {
  ImDrawList* draw_list = canvas_.GetDrawList();
  if (draw_list == nullptr) return;

  for (const ZoneGizmoItem& item : items) {
    ImU32 color = IM_COL32(255, 255, 0, 150);
    float thickness = 2.0f;
    if (item.state == ZoneGizmoState::kActive) {
      color = IM_COL32(80, 220, 120, 220);
    }
    if (item.state == ZoneGizmoState::kSelected) {
      color = IM_COL32(255, 200, 0, 255);
      thickness = 3.0f;
    }
    draw_list->AddRect(canvas_.WorldToScreen(item.bounds.min),
                       canvas_.WorldToScreen(item.bounds.max), color, 0.0f, 0, thickness);
  }
}

}  // namespace zebes
