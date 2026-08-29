#include "curation/generated_asset_quarantine.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "artwork/source_artwork.h"
#include "common/atomic_directory_publisher.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"
#include "objects/blueprint.h"
#include "objects/sprite.h"
#include "objects/texture.h"
#include "resources/resource_utils.h"
#include "resources/texture_manager.h"

namespace zebes {
namespace {

enum class LiveDisposition {
  kRemove,
  kRetainSharedDependency,
};

struct QuarantineFile {
  std::filesystem::path relative_path;
  std::filesystem::path source_path;
  LiveDisposition disposition = LiveDisposition::kRemove;
};

struct QuarantinePlan {
  std::vector<QuarantineFile> files;
};

std::string_view LiveDispositionId(LiveDisposition disposition) {
  switch (disposition) {
    case LiveDisposition::kRemove:
      return "removed";
    case LiveDisposition::kRetainSharedDependency:
      return "retained_shared_dependency";
  }
  return {};
}

bool IsWithin(const std::filesystem::path& parent, const std::filesystem::path& child) {
  auto parent_component = parent.begin();
  auto child_component = child.begin();
  for (; parent_component != parent.end() && child_component != child.end();
       ++parent_component, ++child_component) {
    if (*parent_component != *child_component) return false;
  }
  return parent_component == parent.end();
}

absl::StatusOr<std::filesystem::path> CanonicalPath(const std::filesystem::path& path,
                                                    std::string_view subject) {
  std::error_code error;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
  if (error) {
    return absl::InternalError(absl::StrCat("could not resolve ", subject, ": ", error.message()));
  }
  return canonical;
}

absl::Status ValidateOutputOutsideAssetRoot(const std::filesystem::path& asset_root,
                                            const std::string& output_path) {
  if (output_path.empty()) return absl::InvalidArgumentError("quarantine output path is empty");
  ASSIGN_OR_RETURN(const std::filesystem::path canonical_root,
                   CanonicalPath(asset_root, "asset root"));
  ASSIGN_OR_RETURN(const std::filesystem::path canonical_output,
                   CanonicalPath(output_path, "quarantine output"));
  if (IsWithin(canonical_root, canonical_output)) {
    return absl::InvalidArgumentError("quarantine output must be outside the live asset root");
  }
  return ValidateNewDirectoryDestination(output_path);
}

absl::StatusOr<std::filesystem::path> ResolveAssetFile(const std::filesystem::path& canonical_root,
                                                       const std::filesystem::path& relative_path) {
  if (relative_path.empty() || relative_path.is_absolute()) {
    return absl::InvalidArgumentError("quarantine asset path must be relative");
  }
  const std::filesystem::path normalized = relative_path.lexically_normal();
  if (normalized.empty() || normalized == ".") {
    return absl::InvalidArgumentError("quarantine asset path is empty after normalization");
  }
  for (const std::filesystem::path& component : normalized) {
    if (component == "..") {
      return absl::InvalidArgumentError(
          absl::StrCat("quarantine asset path escapes the asset root: ", relative_path.string()));
    }
  }

  const std::filesystem::path source = canonical_root / normalized;
  std::error_code error;
  const std::filesystem::file_status link_status = std::filesystem::symlink_status(source, error);
  if (error) {
    return absl::InternalError(absl::StrCat("could not inspect quarantine source ",
                                            normalized.string(), ": ", error.message()));
  }
  if (std::filesystem::is_symlink(link_status)) {
    return absl::FailedPreconditionError(
        absl::StrCat("quarantine source must not be a symlink: ", normalized.string()));
  }
  if (!std::filesystem::is_regular_file(link_status)) {
    return absl::NotFoundError(
        absl::StrCat("quarantine source is not a regular file: ", normalized.string()));
  }
  ASSIGN_OR_RETURN(const std::filesystem::path canonical_source,
                   CanonicalPath(source, "quarantine source"));
  if (!IsWithin(canonical_root, canonical_source)) {
    return absl::FailedPreconditionError(
        absl::StrCat("quarantine source resolves outside the asset root: ", normalized.string()));
  }
  return canonical_source;
}

absl::Status AddFile(const std::filesystem::path& canonical_root,
                     const std::filesystem::path& relative_path, LiveDisposition disposition,
                     QuarantinePlan& plan) {
  const std::filesystem::path normalized = relative_path.lexically_normal();
  const auto duplicate = std::ranges::find_if(
      plan.files,
      [&normalized](const QuarantineFile& file) { return file.relative_path == normalized; });
  if (duplicate != plan.files.end()) {
    return absl::FailedPreconditionError(
        absl::StrCat("generated graph names the same file twice: ", normalized.string()));
  }
  ASSIGN_OR_RETURN(std::filesystem::path source, ResolveAssetFile(canonical_root, normalized));
  plan.files.push_back({
      .relative_path = normalized,
      .source_path = std::move(source),
      .disposition = disposition,
  });
  return absl::OkStatus();
}

std::filesystem::path NamedDefinition(std::string_view directory, std::string_view name,
                                      std::string_view id) {
  return std::filesystem::path(directory) / absl::StrCat(name, "-", id, ".json");
}

std::filesystem::path IdDefinition(std::string_view directory, std::string_view id) {
  return std::filesystem::path(directory) / absl::StrCat(id, ".json");
}

bool SourceIsShared(Api& api, std::string_view source_id, GeneratedAssetKind owner_kind,
                    std::string_view owner_id) {
  for (const PropRecipe& recipe : api.GetAllPropRecipes()) {
    if (recipe.source_artwork_id != source_id) continue;
    if (owner_kind != GeneratedAssetKind::kProp || recipe.id != owner_id) return true;
  }
  for (const ParallaxArtworkRecipe& recipe : api.GetAllParallaxArtworkRecipes()) {
    if (recipe.source_artwork_id != source_id) continue;
    if (owner_kind != GeneratedAssetKind::kParallaxArtwork || recipe.id != owner_id) return true;
  }
  return false;
}

absl::Status AddTextureFiles(const std::filesystem::path& root, const Texture& texture,
                             QuarantinePlan& plan) {
  RETURN_IF_ERROR(AddFile(root, NamedDefinition("definitions/textures", texture.name, texture.id),
                          LiveDisposition::kRemove, plan));
  const std::filesystem::path resolved_image = ResolveTextureImagePath(root.string(), texture.path);
  return AddFile(root, resolved_image.lexically_relative(root), LiveDisposition::kRemove, plan);
}

absl::Status AddSourceFiles(const std::filesystem::path& root, const SourceArtwork& source,
                            LiveDisposition disposition, QuarantinePlan& plan) {
  RETURN_IF_ERROR(
      AddFile(root, IdDefinition("definitions/source_artworks", source.id), disposition, plan));
  return AddFile(root, source.source_path, disposition, plan);
}

absl::StatusOr<QuarantinePlan> BuildTerrainPlan(Api& api, const std::filesystem::path& root,
                                                const std::string& recipe_id) {
  RETURN_IF_ERROR(api.CheckGeneratedTerrainDeletable(recipe_id));
  ASSIGN_OR_RETURN(TerrainRecipe * recipe, api.GetTerrainRecipe(recipe_id));
  ASSIGN_OR_RETURN(Tileset * tileset, api.GetTileset(recipe->tileset_id));
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(recipe->texture_id));
  if (tileset->texture_id != texture->id) {
    return absl::FailedPreconditionError(
        "generated terrain tileset no longer names its recipe-owned texture");
  }

  QuarantinePlan plan;
  RETURN_IF_ERROR(AddFile(root, IdDefinition("definitions/terrain_recipes", recipe->id),
                          LiveDisposition::kRemove, plan));
  RETURN_IF_ERROR(AddFile(root, NamedDefinition("definitions/tilesets", tileset->name, tileset->id),
                          LiveDisposition::kRemove, plan));
  RETURN_IF_ERROR(AddTextureFiles(root, *texture, plan));
  return plan;
}

absl::StatusOr<QuarantinePlan> BuildPropPlan(Api& api, const std::filesystem::path& root,
                                             const std::string& recipe_id) {
  RETURN_IF_ERROR(api.CheckGeneratedPropDeletable(recipe_id));
  ASSIGN_OR_RETURN(PropRecipe * recipe, api.GetPropRecipe(recipe_id));
  ASSIGN_OR_RETURN(Blueprint * blueprint, api.GetBlueprint(recipe->blueprint_id));
  ASSIGN_OR_RETURN(Sprite * sprite, api.GetSprite(recipe->sprite_id));
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(recipe->texture_id));
  ASSIGN_OR_RETURN(SourceArtwork * source, api.GetSourceArtwork(recipe->source_artwork_id));
  const bool blueprint_names_sprite = std::ranges::any_of(
      blueprint->states,
      [recipe](const Blueprint::State& state) { return state.sprite_id == recipe->sprite_id; });
  if (!blueprint_names_sprite || sprite->texture_id != texture->id) {
    return absl::FailedPreconditionError(
        "generated prop outputs no longer form the graph recorded by its recipe");
  }

  QuarantinePlan plan;
  RETURN_IF_ERROR(AddFile(root, IdDefinition("definitions/prop_recipes", recipe->id),
                          LiveDisposition::kRemove, plan));
  RETURN_IF_ERROR(AddFile(root,
                          NamedDefinition("definitions/blueprints", blueprint->name, blueprint->id),
                          LiveDisposition::kRemove, plan));
  RETURN_IF_ERROR(AddFile(root, NamedDefinition("definitions/sprites", sprite->name, sprite->id),
                          LiveDisposition::kRemove, plan));
  RETURN_IF_ERROR(AddTextureFiles(root, *texture, plan));
  const LiveDisposition source_disposition =
      SourceIsShared(api, source->id, GeneratedAssetKind::kProp, recipe->id)
          ? LiveDisposition::kRetainSharedDependency
          : LiveDisposition::kRemove;
  RETURN_IF_ERROR(AddSourceFiles(root, *source, source_disposition, plan));
  return plan;
}

absl::StatusOr<QuarantinePlan> BuildParallaxArtworkPlan(Api& api, const std::filesystem::path& root,
                                                        const std::string& recipe_id) {
  RETURN_IF_ERROR(api.CheckGeneratedParallaxArtworkDeletable(recipe_id));
  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * recipe, api.GetParallaxArtworkRecipe(recipe_id));
  ASSIGN_OR_RETURN(Texture * texture, api.GetTexture(recipe->texture_id));
  ASSIGN_OR_RETURN(SourceArtwork * source, api.GetSourceArtwork(recipe->source_artwork_id));

  QuarantinePlan plan;
  RETURN_IF_ERROR(AddFile(root, IdDefinition("definitions/parallax_artwork_recipes", recipe->id),
                          LiveDisposition::kRemove, plan));
  RETURN_IF_ERROR(AddTextureFiles(root, *texture, plan));
  const LiveDisposition source_disposition =
      SourceIsShared(api, source->id, GeneratedAssetKind::kParallaxArtwork, recipe->id)
          ? LiveDisposition::kRetainSharedDependency
          : LiveDisposition::kRemove;
  RETURN_IF_ERROR(AddSourceFiles(root, *source, source_disposition, plan));
  return plan;
}

absl::StatusOr<QuarantinePlan> BuildPlan(Api& api, const std::filesystem::path& root,
                                         GeneratedAssetKind kind, const std::string& recipe_id) {
  switch (kind) {
    case GeneratedAssetKind::kTerrain:
      return BuildTerrainPlan(api, root, recipe_id);
    case GeneratedAssetKind::kProp:
      return BuildPropPlan(api, root, recipe_id);
    case GeneratedAssetKind::kParallaxArtwork:
      return BuildParallaxArtworkPlan(api, root, recipe_id);
  }
  return absl::InvalidArgumentError("unknown generated asset kind");
}

absl::Status PublishRecoverySnapshot(const GeneratedAssetQuarantineOptions& options,
                                     const QuarantinePlan& plan) {
  return PublishNewDirectoryAtomically(
      options.output_path, [&options, &plan](const std::filesystem::path& staging) {
        nlohmann::json files = nlohmann::json::array();
        for (const QuarantineFile& file : plan.files) {
          const std::filesystem::path destination = staging / "assets" / file.relative_path;
          std::error_code error;
          std::filesystem::create_directories(destination.parent_path(), error);
          if (error) {
            return absl::InternalError(
                absl::StrCat("could not create quarantine directory: ", error.message()));
          }
          if (!std::filesystem::copy_file(file.source_path, destination,
                                          std::filesystem::copy_options::none, error) ||
              error) {
            return absl::InternalError(absl::StrCat("could not copy ", file.relative_path.string(),
                                                    " into quarantine: ", error.message()));
          }
          files.push_back({
              {"path", file.relative_path.generic_string()},
              {"live_disposition", LiveDispositionId(file.disposition)},
          });
        }
        const nlohmann::json manifest = {
            {"schema_version", 1},
            {"asset_kind", GeneratedAssetKindId(options.kind)},
            {"recipe_id", options.recipe_id},
            {"files", std::move(files)},
        };
        return WriteTextFileAtomically((staging / "manifest.json").string(), manifest.dump(2));
      });
}

absl::Status DeleteLiveGraph(Api& api, GeneratedAssetKind kind, const std::string& recipe_id) {
  switch (kind) {
    case GeneratedAssetKind::kTerrain:
      return api.DeleteGeneratedTerrain(recipe_id);
    case GeneratedAssetKind::kProp:
      return api.DeleteGeneratedProp(recipe_id);
    case GeneratedAssetKind::kParallaxArtwork:
      return api.DeleteGeneratedParallaxArtwork(recipe_id);
  }
  return absl::InvalidArgumentError("unknown generated asset kind");
}

}  // namespace

absl::StatusOr<GeneratedAssetKind> ParseGeneratedAssetKind(std::string_view kind) {
  if (kind == "terrain") return GeneratedAssetKind::kTerrain;
  if (kind == "prop") return GeneratedAssetKind::kProp;
  if (kind == "parallax-artwork") return GeneratedAssetKind::kParallaxArtwork;
  return absl::InvalidArgumentError(
      "generated asset kind must be terrain, prop, or parallax-artwork");
}

std::string_view GeneratedAssetKindId(GeneratedAssetKind kind) {
  switch (kind) {
    case GeneratedAssetKind::kTerrain:
      return "terrain";
    case GeneratedAssetKind::kProp:
      return "prop";
    case GeneratedAssetKind::kParallaxArtwork:
      return "parallax-artwork";
  }
  return {};
}

absl::Status QuarantineGeneratedAsset(Api& api, const GeneratedAssetQuarantineOptions& options) {
  if (options.asset_root.empty() || options.output_path.empty() || options.recipe_id.empty()) {
    return absl::InvalidArgumentError(
        "asset root, quarantine output, and recipe ID must all be non-empty");
  }
  RETURN_IF_ERROR(ValidateOutputOutsideAssetRoot(options.asset_root, options.output_path));
  ASSIGN_OR_RETURN(const std::filesystem::path canonical_root,
                   CanonicalPath(options.asset_root, "asset root"));
  ASSIGN_OR_RETURN(const QuarantinePlan plan,
                   BuildPlan(api, canonical_root, options.kind, options.recipe_id));
  RETURN_IF_ERROR(PublishRecoverySnapshot(options, plan));

  const absl::Status deleted = DeleteLiveGraph(api, options.kind, options.recipe_id);
  if (!deleted.ok()) {
    return absl::Status(
        deleted.code(),
        absl::StrCat(deleted.message(), "; complete recovery snapshot is available at ",
                     options.output_path));
  }
  return absl::OkStatus();
}

}  // namespace zebes
