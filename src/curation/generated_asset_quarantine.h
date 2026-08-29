#pragma once

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace zebes {

class Api;

enum class GeneratedAssetKind {
  kTerrain,
  kProp,
  kParallaxArtwork,
};

absl::StatusOr<GeneratedAssetKind> ParseGeneratedAssetKind(std::string_view kind);
std::string_view GeneratedAssetKindId(GeneratedAssetKind kind);

struct GeneratedAssetQuarantineOptions {
  std::string asset_root;
  std::string output_path;
  GeneratedAssetKind kind = GeneratedAssetKind::kProp;
  std::string recipe_id;
};

// Publishes a complete recovery snapshot before removing the live generated
// graph through Api's reference-checked bundle deletion. The caller must hold
// exclusive write access to asset_root for the entire operation.
absl::Status QuarantineGeneratedAsset(Api& api, const GeneratedAssetQuarantineOptions& options);

}  // namespace zebes
