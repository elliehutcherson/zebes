#include "absl/status/statusor.h"
#include "api/api.h"
#include "objects/level.h"

namespace zebes {

struct LevelResult {
  enum Type : uint8_t { kNone = 0, kAttach = 1, kDetach = 2 };
  Type type = Type::kNone;
  std::string level_id;
};

class LevelPanel {
 public:
  // Creates a new LevelPanel instance.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<LevelPanel>> Create(Api* api);

  // Renders the level panel UI.
  // This is the main entry point for the panel's rendering logic. This will return "kAttach" or
  // "kDetach" when the user attaches or detaches a valid level.
  absl::StatusOr<LevelResult> Render();

  absl::Status Attach(const std::string& id);

  void Detach();

 private:
  enum Op : uint8_t { kLevelCreate, kLevelUpdate, kLevelDelete, kLevelReset };

  LevelPanel(Api* api);

  absl::Status Attach(int i);

  // Refreshes the local cache of levels from the API.
  void RefreshLevelCache();

  // Renders the list of levels and CRUD buttons.
  absl::StatusOr<LevelResult> RenderList();

  // Renders the details view for creating or editing a level.
  absl::StatusOr<LevelResult> RenderDetails();

  absl::Status ConfirmState(Op op);

  int selected_index_ = -1;
  std::vector<Level> level_cache_;
  std::optional<Level> editting_level_;

  // Outside dependencies
  Api& api_;
};

}  // namespace zebes