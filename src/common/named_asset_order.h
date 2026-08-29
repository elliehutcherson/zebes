#pragma once

namespace zebes {

// Human-facing asset catalogs sort by display name, then by stable ID so
// duplicate names remain deterministic.
struct NamedAssetLess {
  template <typename Asset>
  constexpr bool operator()(const Asset& left, const Asset& right) const {
    if (left.name != right.name) return left.name < right.name;
    return left.id < right.id;
  }
};

}  // namespace zebes
