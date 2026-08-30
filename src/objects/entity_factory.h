#pragma once

#include <cstdint>
#include <string_view>

#include "absl/status/statusor.h"
#include "objects/blueprint.h"
#include "objects/entity.h"
#include "objects/vec.h"

namespace zebes {

// Builds persistent entity state from one blueprint state. Resource pointers
// stay outside the level; the entity records the blueprint-owned asset IDs.
absl::StatusOr<Entity> CreateEntityFromBlueprint(const Blueprint& blueprint,
                                                 std::string_view state_key, Vec world_position,
                                                 uint64_t id);

}  // namespace zebes
