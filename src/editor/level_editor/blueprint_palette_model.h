#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "objects/blueprint.h"

namespace zebes {

struct BlueprintPalettePreviewMetadata {
  std::optional<std::string> sprite_id;

  bool operator==(const BlueprintPalettePreviewMetadata& other) const = default;
};

struct BlueprintPaletteEntry {
  std::string id;
  std::string name;
  BlueprintPalettePreviewMetadata preview;

  bool operator==(const BlueprintPaletteEntry& other) const = default;
};

// Platform-neutral searchable blueprint catalogue. It owns only stable IDs and
// display metadata; the Api remains responsible for resolving live resources.
class BlueprintPaletteModel {
 public:
  absl::Status SetBlueprints(std::span<const Blueprint> blueprints);

  void SetSearchQuery(std::string query) { search_query_ = std::move(query); }
  const std::string& search_query() const { return search_query_; }

  std::vector<const BlueprintPaletteEntry*> FilteredEntries() const;

  absl::Status ToggleSelection(std::string_view blueprint_id);
  void ClearSelection() { selected_blueprint_id_.reset(); }
  const std::optional<std::string>& selected_blueprint_id() const { return selected_blueprint_id_; }

 private:
  const BlueprintPaletteEntry* Find(std::string_view blueprint_id) const;

  std::vector<BlueprintPaletteEntry> entries_;
  std::optional<std::string> selected_blueprint_id_;
  std::string search_query_;
};

}  // namespace zebes
