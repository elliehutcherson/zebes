#pragma once

namespace zebes {

class HudTerminal {
 public:
  HudTerminal() = default;

  void Render();

 private:
  // If the log size hasn't changed, we don't need to update the log.
  int log_size_ = 0;
};

}  // namespace zebes
