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

  // Fails if options.input_source is null.
  static absl::StatusOr<std::unique_ptr<InputManager>> Create(Options options);

  InputManager(const InputManager&) = delete;
  InputManager& operator=(const InputManager&) = delete;

  ~InputManager() override = default;

  void BindAction(absl::string_view action_name, Key key) override;

  void Update() override;

  bool IsActionActive(absl::string_view action_name) const override;

  bool IsActionJustPressed(absl::string_view action_name) const override;

  InputSnapshot CurrentSnapshot() const override;

  bool QuitRequested() const override;

 private:
  explicit InputManager(InputSource& input_source);

  InputSource& input_source_;
  absl::flat_hash_map<std::string, std::vector<Key>> action_bindings_;
  InputSnapshot current_snapshot_;
  InputSnapshot previous_snapshot_;
  bool quit_requested_ = false;
};

}  // namespace zebes
