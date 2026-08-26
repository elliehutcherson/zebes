#pragma once

#include <memory>

#include "generation/image_generation.h"

namespace zebes {

// Deterministic, credential-free provider used by the documented headless
// loop and its acceptance tests.
std::unique_ptr<ImageGenerationClient> CreateFakeImageGenerationClient();

}  // namespace zebes
