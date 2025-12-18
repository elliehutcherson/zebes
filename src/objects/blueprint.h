#pragma once

#include <map>
#include <set>

namespace zebes {

struct Blueprint {
  std::string id;
  // Order is required, the index of the state will be used to find the id sprite_id or collider_id.
  std::set<std::string> states;
  std::map<int, std::string> collider_ids;
  std::map<int, std::string> sprite_ids;
};

}  // namespace zebes