#pragma once

#include <cstdint>

#include "objects/blueprint.h"
#include "objects/entity.h"
#include "objects/vec.h"

namespace zebes {

// Builds persistent entity state from one blueprint state. Resource pointers
// stay outside the level; the entity records the blueprint-owned asset IDs.
Entity CreateEntityFromBlueprint(const Blueprint& blueprint, int state_index, Vec world_position,
                                 uint64_t id);

}  // namespace zebes
