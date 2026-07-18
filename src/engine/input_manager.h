#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "engine/input_manager_interface.h"
#include "engine/input_types.h"

namespace zebes {

class InputManager : public IInputManager {
 public:
  struct Options {
    // Platform input source. Must outlive the manager.
    InputSource* input_source = nullptr;
  };

  // Factory function
  static absl::StatusOr<std::unique_ptr<InputManager>> Create(Options options);

  // Delete copy and move constructors/operators
  InputManager(const InputManager&) = delete;
  InputManager& operator=(const InputManager&) = delete;

  ~InputManager() override = default;

  // --- 1. Registration API ---
  void BindAction(absl::string_view action_name, Key key) override;

  // --- 2. Interception Loop ---
  void Update() override;

  // --- 3. Query API ---
  bool IsActionActive(absl::string_view action_name) const override;

  bool IsActionJustPressed(absl::string_view action_name) const override;

  bool QuitRequested() const override;

 private:
  // Private constructor
  explicit InputManager(InputSource& input_source);

  InputSource& input_source_;
  absl::flat_hash_map<std::string, std::vector<Key>> action_bindings_;
  InputSnapshot current_snapshot_;
  InputSnapshot previous_snapshot_;
  bool quit_requested_ = false;
};

}  // namespace zebes
