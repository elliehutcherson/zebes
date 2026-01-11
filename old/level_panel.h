#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "objects/level.h"

namespace zebes {

struct LevelResult {
  enum Type : uint8_t { kNone = 0, kAttach = 1, kDetach = 2 };
  Type type = Type::kNone;
  std::string level_id;
};

struct LevelCounters {
  int render_list_count = 0;
  int render_details_count = 0;
};

class ILevelPanel {
 public:
  virtual ~ILevelPanel() = default;

  virtual absl::StatusOr<LevelResult> Render() = 0;
  virtual absl::Status Attach(const std::string& id) = 0;
  virtual void Detach() = 0;
  virtual const LevelCounters& GetCounters() const = 0;
};

class LevelPanel : public ILevelPanel {
 public:
  struct Options {
    Api* api;
    std::optional<Level>* editting_level;
  };
  // Creates a new LevelPanel instance.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<LevelPanel>> Create(Options options);

  // Renders the level panel UI.
  // This is the main entry point for the panel's rendering logic. This will return "kAttach" or
  // "kDetach" when the user attaches or detaches a valid level.
  absl::StatusOr<LevelResult> Render() override;

  absl::Status Attach(const std::string& id) override;

  void Detach() override;

  enum Op : uint8_t { kLevelCreate, kLevelUpdate, kLevelDelete, kLevelReset };

  // Handles a level operation (Create, Update, Delete, Reset).
  // Returns kAttach or kDetach if the operation results in a view change.
  // Returns kChanged if the level list was modified but view stays same.
  absl::StatusOr<LevelResult> HandleOp(Op op);

  const LevelCounters& GetCounters() const override { return counters_; }

  void TestOnlySetSelectedIndex(int index) { selected_index_ = index; }

 private:
  explicit LevelPanel(Options options);

  absl::Status Attach(int i);

  // Refreshes the local cache of levels from the API.
  void RefreshLevelCache();

  // Renders the list of levels and CRUD buttons.
  absl::StatusOr<LevelResult> RenderList();

  // Renders the details view for creating or editing a level.
  absl::StatusOr<LevelResult> RenderDetails();

  int selected_index_ = -1;
  std::vector<Level> level_cache_;
  LevelCounters counters_;

  // Outside dependencies
  Api& api_;
  std::optional<Level>& editting_level_;
};

}  // namespace zebes