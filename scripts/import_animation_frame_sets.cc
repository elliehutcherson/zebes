#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/asset_workspace.h"
#include "api/source_artwork_retention.h"
#include "artwork/delete_animation_frame_set_asset.h"
#include "artwork/prepare_animation_frame_set_asset.h"
#include "common/config.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "nlohmann/json.hpp"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Writable asset root");
ABSL_FLAG(std::string, manifest, "", "Versioned animation frame-set import manifest");

namespace zebes {
namespace {

constexpr int kFrameSize = 48;
constexpr int kOriginX = 24;
constexpr int kOriginY = 44;
constexpr int kContactLineY = 44;

struct ClipImport {
  std::string name;
  std::string state_key;
  std::filesystem::path source;
  std::string texture_id;
  std::string sprite_id;
  std::string recipe_id;
  SpritePlaybackMode playback_mode = SpritePlaybackMode::kLoop;
  std::vector<int> frames_per_cycle;
  std::vector<bool> planted_frames;
};

struct ImportManifest {
  std::string blueprint_id;
  std::vector<ClipImport> clips;
};

absl::StatusOr<std::string> ReadText(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open import manifest: ", path.string()));
  }
  std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (stream.bad()) {
    return absl::DataLossError(absl::StrCat("could not read import manifest: ", path.string()));
  }
  return contents;
}

template <typename T>
absl::StatusOr<T> Required(const nlohmann::json& object, std::string_view key,
                           std::string_view context) {
  const std::string field(key);
  if (!object.is_object() || !object.contains(field)) {
    return absl::InvalidArgumentError(absl::StrCat(context, " is missing '", key, "'"));
  }
  try {
    return object.at(field).get<T>();
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " field '", key, "' is invalid: ", error.what()));
  }
}

absl::Status RequireExactObject(const nlohmann::json& object,
                                std::initializer_list<std::string_view> fields,
                                std::string_view context) {
  if (!object.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat(context, " must be an object"));
  }
  std::set<std::string> expected;
  for (std::string_view field : fields) expected.insert(std::string(field));
  std::set<std::string> actual;
  for (nlohmann::json::const_iterator iterator = object.begin(); iterator != object.end();
       ++iterator) {
    actual.insert(iterator.key());
  }
  if (actual != expected) {
    return absl::InvalidArgumentError(absl::StrCat(context, " has unexpected or missing fields"));
  }
  return absl::OkStatus();
}

absl::StatusOr<SpritePlaybackMode> ParsePlaybackMode(std::string_view value) {
  if (value == "loop") return SpritePlaybackMode::kLoop;
  if (value == "hold-last") return SpritePlaybackMode::kHoldLast;
  return absl::InvalidArgumentError("clip playback_mode must be loop or hold-last");
}

absl::StatusOr<std::filesystem::path> ResolveSource(const std::filesystem::path& manifest_path,
                                                    std::string_view relative_path) {
  const std::filesystem::path relative(relative_path);
  if (relative.empty() || relative.is_absolute()) {
    return absl::InvalidArgumentError("clip source must be a relative path");
  }
  std::error_code error;
  const std::filesystem::path root =
      std::filesystem::weakly_canonical(manifest_path.parent_path(), error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not resolve manifest directory: ", error.message()));
  }
  const std::filesystem::path source = std::filesystem::weakly_canonical(root / relative, error);
  if (error) {
    return absl::InvalidArgumentError(
        absl::StrCat("could not resolve clip source: ", error.message()));
  }
  const std::filesystem::path beneath = source.lexically_relative(root);
  if (beneath.empty() || beneath == "." || *beneath.begin() == "..") {
    return absl::PermissionDeniedError("clip source escapes the manifest directory");
  }
  return source;
}

absl::StatusOr<ClipImport> ParseClip(const nlohmann::json& json,
                                     const std::filesystem::path& manifest_path, size_t index) {
  const std::string context = absl::StrCat("clips[", index, "]");
  RETURN_IF_ERROR(
      RequireExactObject(json,
                         {"name", "state_key", "source", "texture_id", "sprite_id", "recipe_id",
                          "playback_mode", "frames_per_cycle", "planted_frames"},
                         context));
  ClipImport clip;
  ASSIGN_OR_RETURN(clip.name, Required<std::string>(json, "name", context));
  ASSIGN_OR_RETURN(clip.state_key, Required<std::string>(json, "state_key", context));
  ASSIGN_OR_RETURN(const std::string source, Required<std::string>(json, "source", context));
  ASSIGN_OR_RETURN(clip.source, ResolveSource(manifest_path, source));
  ASSIGN_OR_RETURN(clip.texture_id, Required<std::string>(json, "texture_id", context));
  ASSIGN_OR_RETURN(clip.sprite_id, Required<std::string>(json, "sprite_id", context));
  ASSIGN_OR_RETURN(clip.recipe_id, Required<std::string>(json, "recipe_id", context));
  ASSIGN_OR_RETURN(const std::string playback,
                   Required<std::string>(json, "playback_mode", context));
  ASSIGN_OR_RETURN(clip.playback_mode, ParsePlaybackMode(playback));
  ASSIGN_OR_RETURN(clip.frames_per_cycle,
                   Required<std::vector<int>>(json, "frames_per_cycle", context));
  ASSIGN_OR_RETURN(clip.planted_frames,
                   Required<std::vector<bool>>(json, "planted_frames", context));
  if (clip.frames_per_cycle.empty() || clip.frames_per_cycle.size() != clip.planted_frames.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat(context, " timing and planted arrays must have equal nonzero length"));
  }
  if (std::ranges::any_of(clip.frames_per_cycle, [](int timing) { return timing <= 0; })) {
    return absl::InvalidArgumentError(absl::StrCat(context, " frame timing must be positive"));
  }
  return clip;
}

absl::StatusOr<ImportManifest> ParseManifest(const std::filesystem::path& path) {
  ASSIGN_OR_RETURN(const std::string contents, ReadText(path));
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(contents);
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat("invalid import manifest JSON: ", error.what()));
  }
  RETURN_IF_ERROR(RequireExactObject(json, {"schema_version", "blueprint_id", "clips"},
                                     "animation import manifest"));
  ASSIGN_OR_RETURN(const int version, Required<int>(json, "schema_version", "manifest"));
  if (version != 1) return absl::InvalidArgumentError("unsupported import manifest version");
  ImportManifest manifest;
  ASSIGN_OR_RETURN(manifest.blueprint_id, Required<std::string>(json, "blueprint_id", "manifest"));
  ASSIGN_OR_RETURN(const nlohmann::json clips, Required<nlohmann::json>(json, "clips", "manifest"));
  if (!clips.is_array() || clips.empty()) {
    return absl::InvalidArgumentError("manifest clips must be a nonempty array");
  }
  std::set<std::string> state_keys;
  std::set<std::string> ids;
  manifest.clips.reserve(clips.size());
  for (size_t index = 0; index < clips.size(); ++index) {
    ASSIGN_OR_RETURN(ClipImport clip, ParseClip(clips[index], path, index));
    if (!state_keys.insert(clip.state_key).second) {
      return absl::InvalidArgumentError("manifest repeats a Blueprint state key");
    }
    for (const std::string* id : {&clip.texture_id, &clip.sprite_id, &clip.recipe_id}) {
      if (!ids.insert(*id).second) {
        return absl::InvalidArgumentError("manifest repeats an output asset ID");
      }
    }
    manifest.clips.push_back(std::move(clip));
  }
  return manifest;
}

AnimationFrameSetStyle ProductionStyle() {
  return {
      .extraction = AnimationFrameSetExtraction::kPreserveAlpha,
      .matte = {255, 0, 255, 255},
      .transparent_matte_distance = 24.0f,
      .opaque_matte_distance = 190.0f,
      .alpha_threshold = 128,
      .palette =
          {
              {33, 29, 26, 255},
              {124, 75, 53, 255},
              {207, 158, 124, 255},
              {247, 230, 201, 255},
              {242, 185, 188, 255},
              {255, 214, 211, 255},
              {142, 170, 135, 255},
              {166, 188, 156, 255},
              {108, 137, 105, 255},
              {222, 53, 44, 255},
              {249, 80, 56, 255},
              {156, 105, 70, 255},
              {90, 45, 31, 255},
              {184, 124, 75, 255},
              {227, 81, 98, 255},
          },
  };
}

AnimationFrameSetPipelineConfig ProductionPipeline(const ClipImport& clip) {
  const int frame_count = static_cast<int>(clip.frames_per_cycle.size());
  return {
      .source_limits = {},
      .sheet =
          {
              .grid_x = 0,
              .grid_y = 0,
              .cell_width = kFrameSize,
              .cell_height = kFrameSize,
              .column_gap = 0,
              .row_gap = 0,
              .columns = frame_count,
              .rows = 1,
          },
      .output_width = kFrameSize,
      .output_height = kFrameSize,
      .origin_x = kOriginX,
      .origin_y = kOriginY,
      .contact_line_y = kContactLineY,
      .render_scale = 2,
      .contact_tolerance = 2,
      .minimum_visible_pixels = 64,
      .maximum_horizontal_anchor_drift = 10,
      .maximum_vertical_anchor_drift = clip.playback_mode == SpritePlaybackMode::kHoldLast ? 8 : 4,
      .packing_columns = frame_count,
      .playback_mode = clip.playback_mode,
      .frames_per_cycle = clip.frames_per_cycle,
      .planted_frames = clip.planted_frames,
  };
}

absl::Status DeleteImportedRecipe(Api& api, const std::string& recipe_id) {
  ASSIGN_OR_RETURN(AnimationFrameSetRecipe * recipe, api.GetAnimationFrameSetRecipe(recipe_id));
  ASSIGN_OR_RETURN(SourceArtwork * source, api.GetSourceArtwork(recipe->source_artwork_id));
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(recipe->texture_id));
  ASSIGN_OR_RETURN(RgbaImage texture_pixels, api.ReadTexturePixels(texture->id));
  ASSIGN_OR_RETURN(Sprite * sprite, api.GetSprite(recipe->sprite_id));
  ASSIGN_OR_RETURN(Blueprint * blueprint, api.GetBlueprint(recipe->blueprint_id));
  ASSIGN_OR_RETURN(PreparedAnimationFrameSetDeletion deletion,
                   PrepareAnimationFrameSetDeletion(*source, *recipe, *texture, texture_pixels,
                                                    *sprite, *blueprint));
  return api.DeleteAnimationFrameSet(deletion);
}

absl::Status RollBack(Api& api, const std::vector<std::string>& recipe_ids) {
  absl::Status result = absl::OkStatus();
  for (auto iterator = recipe_ids.rbegin(); iterator != recipe_ids.rend(); ++iterator) {
    const absl::Status status = DeleteImportedRecipe(api, *iterator);
    if (status.ok()) continue;
    LOG(ERROR) << "Could not roll back animation frame set " << *iterator << ": " << status;
    if (result.ok()) result = status;
  }
  return result;
}

absl::Status ImportClip(Api& api, const ImportManifest& manifest, const ClipImport& clip) {
  ASSIGN_OR_RETURN(const RgbaImage pixels, ReadPng(clip.source.string()));
  const int frame_count = static_cast<int>(clip.frames_per_cycle.size());
  if (pixels.width != frame_count * kFrameSize || pixels.height != kFrameSize) {
    return absl::InvalidArgumentError(
        absl::StrCat(clip.name, " source must be ", frame_count * kFrameSize, "x", kFrameSize));
  }
  ImportedArtworkProvenance provenance{
      .original_filename = clip.source.filename().string(),
      .imported_at_utc = CurrentUtcTimestamp(),
  };
  ASSIGN_OR_RETURN(
      const std::string source_id,
      RetainSourceArtwork(
          api, absl::StrCat(clip.name, " source"), std::move(provenance), pixels,
          [&](const SourceArtwork& source, const RgbaImage& retained_pixels) -> absl::Status {
            ASSIGN_OR_RETURN(Blueprint * blueprint, api.GetBlueprint(manifest.blueprint_id));
            PrepareAnimationFrameSetAssetRequest request{
                .name = clip.name,
                .style = ProductionStyle(),
                .pipeline = ProductionPipeline(clip),
                .ids =
                    {
                        .texture_id = clip.texture_id,
                        .sprite_id = clip.sprite_id,
                        .recipe_id = clip.recipe_id,
                    },
                .blueprint_state_keys = {clip.state_key},
            };
            ASSIGN_OR_RETURN(
                PreparedAnimationFrameSetAsset prepared,
                PrepareAnimationFrameSetAsset(source, retained_pixels, *blueprint, request));
            RETURN_IF_ERROR(api.CreateAnimationFrameSet(prepared).status());
            return absl::OkStatus();
          }));
  std::cout << clip.state_key << " source=" << source_id << " recipe=" << clip.recipe_id << '\n';
  return absl::OkStatus();
}

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string manifest_path = absl::GetFlag(FLAGS_manifest);
  if (asset_root.empty() || manifest_path.empty()) {
    return absl::InvalidArgumentError("--asset_root and --manifest must be nonempty");
  }
  ASSIGN_OR_RETURN(const ImportManifest manifest, ParseManifest(manifest_path));
  ASSIGN_OR_RETURN(EngineConfig config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(std::unique_ptr<AssetWorkspace> workspace,
                   AssetWorkspace::Create({
                       .config = &config,
                       .texture_resources = &texture_resources,
                       .asset_root = asset_root,
                       .access = AssetWorkspace::Access::kReadWrite,
                       .load_profile = AssetWorkspace::LoadProfile::kComplete,
                   }));

  std::vector<std::string> committed_recipes;
  for (const ClipImport& clip : manifest.clips) {
    const absl::Status status = ImportClip(workspace->api(), manifest, clip);
    if (!status.ok()) {
      const absl::Status rollback = RollBack(workspace->api(), committed_recipes);
      if (!rollback.ok()) {
        return absl::InternalError(absl::StrCat("import failed: ", status.message(),
                                                "; rollback failed: ", rollback.message()));
      }
      return status;
    }
    committed_recipes.push_back(clip.recipe_id);
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << "Animation frame-set import failed: " << status;
    return 1;
  }
  return 0;
}
