#pragma once

#include <array>
#include <cstddef>

namespace zebes {

// Engine-owned physical keys. Platform adapters translate native key codes
// into these values before input reaches engine logic.
enum class Key : std::size_t {
  kA,
  kD,
  kE,
  kF,
  kQ,
  kS,
  kW,
  kSpace,
  kRight,
  kCount,
};

struct InputSnapshot {
  std::array<bool, static_cast<std::size_t>(Key::kCount)> keys{};
  bool quit_requested = false;

  bool IsKeyDown(Key key) const { return keys[static_cast<std::size_t>(key)]; }
  void SetKeyDown(Key key, bool down = true) { keys[static_cast<std::size_t>(key)] = down; }
};

class InputSource {
 public:
  virtual ~InputSource() = default;
  virtual InputSnapshot Poll() = 0;
};

}  // namespace zebes
