#pragma once

#include <optional>
#include <string>
#include <vector>

namespace zebes {

struct Blueprint {
  struct State {
    std::string name;
    std::string collider_id;
    std::string sprite_id;
  };

  std::string id;
  std::string name;
  std::vector<State> states;

  std::optional<std::string> collider_id(int index) {
    if (index < 0 || index >= states.size()) {
      return std::nullopt;
    }
    const State& state = states[index];
    if (state.collider_id.empty()) {
      return std::nullopt;
    }
    return state.collider_id;
  }

  std::optional<std::string> sprite_id(int index) {
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