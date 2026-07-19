#pragma once

#include <string>

namespace zebes {

// Orders named assets for deterministic UI iteration while preserving assets
// that share the same display name.
struct AssetCatalogKey {
  std::string display_name;
  std::string id;

  friend bool operator<(const AssetCatalogKey& lhs, const AssetCatalogKey& rhs) {
    if (lhs.display_name != rhs.display_name) return lhs.display_name < rhs.display_name;
    return lhs.id < rhs.id;
  }
};

}  // namespace zebes
