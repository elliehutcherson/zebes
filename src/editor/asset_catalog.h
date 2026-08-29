#pragma once

#include <compare>
#include <string>

namespace zebes {

// Orders named assets for deterministic UI iteration while preserving assets
// that share the same display name.
struct AssetCatalogKey {
  std::string display_name;
  std::string id;

  friend bool operator==(const AssetCatalogKey&, const AssetCatalogKey&) = default;
  friend std::strong_ordering operator<=>(const AssetCatalogKey&, const AssetCatalogKey&) = default;
};

}  // namespace zebes
