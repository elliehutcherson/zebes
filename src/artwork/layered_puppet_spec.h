#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "artwork/layered_puppet.h"
#include "common/image_io.h"

namespace zebes {

// See-through layer inputs, required before any part may name a semantic tag.
// root locates the per-tag PNGs and encoded_metadata is that directory's
// info.json. Passing one without the other is not expressible.
struct LayeredPuppetSemanticSource {
  std::filesystem::path root;
  std::string encoded_metadata;
};

// What one authored stretch actually invented. filled_pixels is the honest
// measure of how much texture was repeated: a large count means the artwork
// behind the moving part is missing, not that the stretch worked well.
struct LayeredPuppetSpecStretchReport {
  std::string part_name;
  size_t required_pixels = 0;
  size_t filled_pixels = 0;
  size_t unreachable_pixels = 0;
  size_t cleared_outside_required_pixels = 0;
};

// Everything one spec produces. puppet is the render input. The three image
// maps are evidence keyed by the part that owns each: what a static layer was
// required to backfill, how strongly a garment stays attached to a moving
// limb, and the untouched semantic layer a part declared immutable. A caller
// that only renders can ignore all three.
struct LayeredPuppetSpecBuild {
  LayeredPuppet puppet;
  std::vector<LayeredPuppetSpecStretchReport> stretch_reports;
  absl::flat_hash_map<std::string, RgbaImage> backfill_requirements;
  absl::flat_hash_map<std::string, RgbaImage> attachment_relationships;
  absl::flat_hash_map<std::string, RgbaImage> immutable_semantic_layers;
};

// Builds a validated puppet from schema-version-1 spec JSON and its source
// artwork. Part ownership, hidden-surface fills, meshes, and per-pose draw
// order all come from the spec; nothing is inferred from the pixels. The
// returned puppet has already passed ValidateLayeredPuppet, so a caller never
// receives a partially built one.
//
// Spec coordinates are in the source image's pixel space, and the spec's
// declared width and height must equal the source's.
//
// semantic may be null. A spec whose parts name a semantic tag requires it and
// fails without it. Malformed JSON becomes an InvalidArgument or DataLoss
// status rather than an exception, so callers stay in the status error model.
absl::StatusOr<LayeredPuppetSpecBuild> BuildLayeredPuppetFromSpec(
    const RgbaImage& source, std::string_view encoded_spec,
    const LayeredPuppetSemanticSource* semantic);

}  // namespace zebes
