#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"

namespace zebes {

// How an authored blueprint origin relates to the tile cell chosen during
// placement. This is blueprint behavior, not sprite geometry: frame render
// offsets remain relative to the same origin for every mode.
enum class BlueprintPlacementMode : uint8_t {
  kGrounded = 0,
  kCeiling = 1,
  kFree = 2,
};

constexpr std::string_view BlueprintPlacementModeId(BlueprintPlacementMode mode) {
  switch (mode) {
    case BlueprintPlacementMode::kGrounded:
      return "grounded";
    case BlueprintPlacementMode::kCeiling:
      return "ceiling";
    case BlueprintPlacementMode::kFree:
      return "free";
  }
  return {};
}

constexpr bool IsValidBlueprintPlacementMode(BlueprintPlacementMode mode) {
  return !BlueprintPlacementModeId(mode).empty();
}

struct Blueprint {
  struct State {
    std::string name;
    std::string collider_id;
    std::string sprite_id;
    BlueprintPlacementMode placement_mode = BlueprintPlacementMode::kGrounded;

    bool operator==(const State& other) const = default;
  };

  std::string id;
  std::string name;
  std::vector<State> states;

  bool operator==(const Blueprint& other) const = default;

  std::string name_id() const { return absl::StrCat(name, "-", id); }

  std::optional<std::string> collider_id(int index) const {
    if (index < 0 || index >= states.size()) {
      return std::nullopt;
    }
    const State& state = states[index];
    if (state.collider_id.empty()) {
      return std::nullopt;
    }
    return state.collider_id;
  }

  std::optional<std::string> sprite_id(int index) const {
    if (index < 0 || index >= states.size()) {
      return std::nullopt;
    }
    const State& state = states[index];
    if (state.sprite_id.empty()) {
      return std::nullopt;
    }
    return state.sprite_id;
  }
};

}  // namespace zebes
