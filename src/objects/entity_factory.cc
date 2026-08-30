#include "objects/entity_factory.h"

#include <optional>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {

absl::StatusOr<Entity> CreateEntityFromBlueprint(const Blueprint& blueprint,
                                                 std::string_view state_key, Vec world_position,
                                                 uint64_t id) {
  const std::optional<int> state_index = blueprint.state_index(state_key);
  if (!state_index.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("Cannot create entity from blueprint '",
                                                   blueprint.id, "' state key '", state_key, "'"));
  }

  const Blueprint::State& state = blueprint.states[*state_index];
  Entity entity;
  entity.id = id;
  entity.blueprint_id = blueprint.id;
  entity.blueprint_state_key = state.key;
  entity.transform.position = world_position;
  entity.sprite_id = state.sprite_id;
  entity.collider_id = state.collider_id;
  return entity;
}

}  // namespace zebes
