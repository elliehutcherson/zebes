#pragma once

#include <string>

#include "artwork/layered_puppet.h"

namespace zebes {

// Encodes a built puppet's geometry for the browser: the bind skeleton, each
// part's bone chain and deformation mesh, and every pose's joints and draw
// order. Pixels are not included; the page fetches each part's artwork by name
// and warps it with this.
//
// This is derived output, regenerated on every rebuild and never stored. The
// document remains the only thing anyone keeps.
std::string LayeredPuppetGeometryJson(const LayeredPuppet& puppet);

}  // namespace zebes
