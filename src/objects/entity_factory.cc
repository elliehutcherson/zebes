#include "objects/entity_factory.h"

namespace zebes {

Entity CreateEntityFromBlueprint(const Blueprint& blueprint, int state_index, Vec world_position,
                                 uint64_t id) {
  Entity entity;
  entity.id = id;
  entity.blueprint_id = blueprint.id;
  entity.blueprint_state_index = state_index;
  entity.transform.position = world_position;
  entity.sprite_id = blueprint.sprite_id(state_index).value_or("");
  entity.collider_id = blueprint.collider_id(state_index).value_or("");
  return entity;
}

}  // namespace zebes
