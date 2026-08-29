#include "editor/level_editor/blueprint_palette_model.h"

#include <algorithm>
#include <set>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "common/named_asset_order.h"

namespace zebes {

absl::Status BlueprintPaletteModel::SetBlueprints(std::span<const Blueprint> blueprints) {
  std::vector<BlueprintPaletteEntry> entries;
  entries.reserve(blueprints.size());
  std::set<std::string> ids;
  for (const Blueprint& blueprint : blueprints) {
    if (blueprint.id.empty()) {
      return absl::InvalidArgumentError("blueprint palette entry has an empty ID");
    }
    if (!ids.insert(blueprint.id).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("duplicate blueprint palette ID: ", blueprint.id));
    }

    BlueprintPalettePreviewMetadata preview;
    if (!blueprint.states.empty() && !blueprint.states.front().sprite_id.empty()) {
      preview.sprite_id = blueprint.states.front().sprite_id;
    }
    entries.push_back({.id = blueprint.id, .name = blueprint.name, .preview = std::move(preview)});
  }

  std::ranges::sort(entries, NamedAssetLess{});
  entries_ = std::move(entries);
  if (selected_blueprint_id_.has_value() && Find(*selected_blueprint_id_) == nullptr) {
    selected_blueprint_id_.reset();
  }
  return absl::OkStatus();
}

std::vector<const BlueprintPaletteEntry*> BlueprintPaletteModel::FilteredEntries() const {
  std::vector<const BlueprintPaletteEntry*> filtered;
  for (const BlueprintPaletteEntry& entry : entries_) {
    if (!search_query_.empty() && !absl::StrContainsIgnoreCase(entry.name, search_query_)) {
      continue;
    }
    filtered.push_back(&entry);
  }
  return filtered;
}

absl::Status BlueprintPaletteModel::ToggleSelection(std::string_view blueprint_id) {
  if (Find(blueprint_id) == nullptr) {
    return absl::NotFoundError(absl::StrCat("blueprint palette ID not found: ", blueprint_id));
  }
  if (selected_blueprint_id_.has_value() && *selected_blueprint_id_ == blueprint_id) {
    selected_blueprint_id_.reset();
  } else {
    selected_blueprint_id_ = std::string(blueprint_id);
  }
  return absl::OkStatus();
}

const BlueprintPaletteEntry* BlueprintPaletteModel::Find(std::string_view blueprint_id) const {
  auto found = std::find_if(entries_.begin(), entries_.end(),
                            [&](const auto& entry) { return entry.id == blueprint_id; });
  return found == entries_.end() ? nullptr : &*found;
}

}  // namespace zebes
