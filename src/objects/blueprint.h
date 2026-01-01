#pragma once

#include <map>
#include <set>

namespace zebes {

struct Blueprint {
  std::string id;
  std::string name;
  // Order is required, the index of the state will be used to find the id sprite_id or collider_id.
  std::set<std::string> states;
  std::map<int, std::string> collider_ids;
  std::map<int, std::string> sprite_ids;

  std::optional<std::string> collider_id(int state_index) {
    auto it = collider_ids.find(state_index);
    if (it == collider_ids.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::optional<std::string> sprite_id(int state_index) {
    auto it = sprite_ids.find(state_index);
    if (it == sprite_ids.end()) {
      return std::nullopt;
    }
    return it->second;
  }
};

}  // namespace zebes