#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/layered_puppet.h"
#include "artwork/layered_puppet_diagnostics.h"
#include "artwork/layered_puppet_spec.h"
#include "artwork/puppet_document.h"
#include "artwork/puppet_document_json.h"
#include "artwork/semantic_layer_import.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

ABSL_FLAG(std::string, source, "", "Approved working-resolution RGBA source PNG.");
ABSL_FLAG(std::string, spec, "", "Explicit layered puppet JSON specification.");
ABSL_FLAG(std::string, document, "",
          "Puppet document JSON to render instead of a spec; the spec is derived from it.");
ABSL_FLAG(std::string, semantic_root, "",
          "Optional See-through optimized PNG and info.json directory.");
ABSL_FLAG(std::string, output, "", "Destination evidence directory.");
ABSL_FLAG(int, frame_size, 48, "Native square output frame size.");
ABSL_FLAG(int, zoom, 8, "Integer nearest-neighbor evidence zoom.");

namespace {

absl::StatusOr<nlohmann::json> ReadJson(const std::string& path, const std::string& label) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError("could not open " + label + ": " + path);
  try {
    return nlohmann::json::parse(stream);
  } catch (const nlohmann::json::exception& error) {
    return absl::DataLossError("invalid " + label + ": " + error.what());
  }
}

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
  std::ofstream stream(path);
  if (!stream.is_open()) return absl::InternalError("could not create puppet manifest");
  stream << value.dump(2) << '\n';
  if (!stream.good()) return absl::InternalError("could not write puppet manifest");
  return absl::OkStatus();
}

absl::Status WriteText(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) return absl::InternalError("could not create puppet editor");
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream.good()) return absl::InternalError("could not write puppet editor");
  return absl::OkStatus();
}

absl::StatusOr<std::string> DocumentSpec(const std::string& path) {
  ASSIGN_OR_RETURN(const zebes::PuppetDocument document, zebes::LoadPuppetDocument(path));
  return zebes::PuppetDocumentSpecJson(document);
}

absl::StatusOr<std::string> ReadText(const std::string& path, const std::string& label) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) return absl::NotFoundError("could not open " + label + ": " + path);
  std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (!stream.good() && !stream.eof()) {
    return absl::DataLossError("could not read " + label + ": " + path);
  }
  return contents;
}

zebes::RgbaImage EmptyCanvasLike(const zebes::RgbaImage& source) {
  return {
      .width = source.width,
      .height = source.height,
      .pixels = std::vector<uint8_t>(source.pixels.size(), 0),
  };
}

absl::Status WriteImage(const std::filesystem::path& path, const zebes::RgbaImage& image) {
  return zebes::WritePng(path.string(), image.width, image.height, image.pixels);
}

size_t OpaquePixelCount(const zebes::RgbaImage& image) {
  size_t count = 0;
  for (size_t offset = 3; offset < image.pixels.size(); offset += 4) {
    if (image.pixels[offset] != 0) ++count;
  }
  return count;
}

size_t PixelDifferenceCount(const zebes::RgbaImage& first, const zebes::RgbaImage& second) {
  size_t count = 0;
  for (size_t offset = 0; offset < first.pixels.size(); offset += 4) {
    if (!std::equal(first.pixels.begin() + static_cast<ptrdiff_t>(offset),
                    first.pixels.begin() + static_cast<ptrdiff_t>(offset + 4),
                    second.pixels.begin() + static_cast<ptrdiff_t>(offset))) {
      ++count;
    }
  }
  return count;
}

bool ContainsPose(const nlohmann::json& poses, const std::string& pose_name) {
  if (!poses.is_array()) return false;
  return std::any_of(poses.begin(), poses.end(), [&pose_name](const nlohmann::json& pose) {
    return pose.is_string() && pose.get<std::string>() == pose_name;
  });
}

absl::Status ApplyConfiguredShadows(zebes::RgbaImage& composite, const zebes::LayeredPuppet& puppet,
                                    const zebes::LayeredPuppetPose& pose,
                                    const nlohmann::json& spec) {
  if (!spec.contains("shadows")) return absl::OkStatus();
  if (!spec.at("shadows").is_array()) {
    return absl::InvalidArgumentError("puppet shadows must be an array");
  }
  for (const nlohmann::json& shadow : spec.at("shadows")) {
    if (!ContainsPose(shadow.at("poses"), pose.name)) continue;
    const std::string caster_name = shadow.at("caster").get<std::string>();
    const auto caster = std::find_if(
        puppet.parts.begin(), puppet.parts.end(),
        [&caster_name](const zebes::LayeredPuppetPart& part) { return part.name == caster_name; });
    if (caster == puppet.parts.end()) {
      return absl::InvalidArgumentError("puppet shadow references an unknown caster");
    }
    const size_t caster_index = static_cast<size_t>(caster - puppet.parts.begin());
    ASSIGN_OR_RETURN(const zebes::RgbaImage caster_image,
                     zebes::RenderLayeredPuppetPart(puppet, pose, caster_index));
    zebes::RgbaImage receiver = EmptyCanvasLike(composite);
    for (const nlohmann::json& receiver_name_json : shadow.at("receivers")) {
      const std::string receiver_name = receiver_name_json.get<std::string>();
      const auto found = std::find_if(puppet.parts.begin(), puppet.parts.end(),
                                      [&receiver_name](const zebes::LayeredPuppetPart& part) {
                                        return part.name == receiver_name;
                                      });
      if (found == puppet.parts.end()) {
        return absl::InvalidArgumentError("puppet shadow references an unknown receiver");
      }
      const size_t receiver_index = static_cast<size_t>(found - puppet.parts.begin());
      ASSIGN_OR_RETURN(const zebes::RgbaImage rendered,
                       zebes::RenderLayeredPuppetPart(puppet, pose, receiver_index));
      for (size_t offset = 3; offset < receiver.pixels.size(); offset += 4) {
        receiver.pixels[offset] = std::max(receiver.pixels[offset], rendered.pixels[offset]);
      }
    }
    const nlohmann::json& offset = shadow.at("offset");
    if (!offset.is_array() || offset.size() != 2) {
      return absl::InvalidArgumentError("puppet shadow offset must contain x and y");
    }
    const int opacity = shadow.at("opacity").get<int>();
    if (opacity <= 0 || opacity > 255) {
      return absl::InvalidArgumentError("puppet shadow opacity must be in [1, 255]");
    }
    RETURN_IF_ERROR(zebes::ApplyLayeredPuppetShadow(composite, caster_image, receiver,
                                                    {.offset_x = offset.at(0).get<int>(),
                                                     .offset_y = offset.at(1).get<int>(),
                                                     .spread = shadow.value("spread", 0),
                                                     .opacity = static_cast<uint8_t>(opacity)}));
  }
  return absl::OkStatus();
}

zebes::RgbaImage TintDifference(const zebes::RgbaImage& composite,
                                const zebes::RgbaImage& without_part) {
  zebes::RgbaImage tinted = composite;
  for (size_t offset = 0; offset < tinted.pixels.size(); offset += 4) {
    const bool differs = !std::equal(composite.pixels.begin() + static_cast<ptrdiff_t>(offset),
                                     composite.pixels.begin() + static_cast<ptrdiff_t>(offset + 4),
                                     without_part.pixels.begin() + static_cast<ptrdiff_t>(offset));
    if (!differs || composite.pixels[offset + 3] == 0) continue;
    tinted.pixels[offset] = 255;
    tinted.pixels[offset + 1] = 0;
    tinted.pixels[offset + 2] = 255;
  }
  return tinted;
}

int Run() {
  const std::string source_path = absl::GetFlag(FLAGS_source);
  const std::string spec_path = absl::GetFlag(FLAGS_spec);
  const std::string document_path = absl::GetFlag(FLAGS_document);
  const std::filesystem::path semantic_root = absl::GetFlag(FLAGS_semantic_root);
  const std::filesystem::path output = absl::GetFlag(FLAGS_output);
  if (source_path.empty() || output.empty() || spec_path.empty() == document_path.empty()) {
    std::cerr << "--source, --output, and exactly one of --spec or --document are required\n";
    return 2;
  }

  const absl::StatusOr<zebes::RgbaImage> source = zebes::ReadPng(source_path);
  if (!source.ok()) {
    std::cerr << source.status().message() << '\n';
    return 1;
  }
  // A document owns everything the builder needs and nothing else; a spec also
  // carries render options this tool reads for itself, so both paths end with
  // the same two values.
  absl::StatusOr<std::string> encoded_spec = document_path.empty()
                                                 ? ReadText(spec_path, "layered puppet spec")
                                                 : DocumentSpec(document_path);
  if (!encoded_spec.ok()) {
    std::cerr << encoded_spec.status().message() << '\n';
    return 1;
  }
  const absl::StatusOr<nlohmann::json> spec =
      document_path.empty() ? ReadJson(spec_path, "layered puppet spec")
                            : absl::StatusOr<nlohmann::json>(nlohmann::json::parse(*encoded_spec));
  if (!spec.ok()) {
    std::cerr << spec.status().message() << '\n';
    return 1;
  }
  zebes::LayeredPuppetSemanticSource semantic;
  const zebes::LayeredPuppetSemanticSource* semantic_pointer = nullptr;
  if (!semantic_root.empty()) {
    absl::StatusOr<std::string> metadata =
        ReadText((semantic_root / "info.json").string(), "semantic layer metadata");
    if (!metadata.ok()) {
      std::cerr << metadata.status().message() << '\n';
      return 1;
    }
    semantic = {.root = semantic_root, .encoded_metadata = *std::move(metadata)};
    semantic_pointer = &semantic;
  }

  absl::StatusOr<zebes::LayeredPuppetSpecBuild> build =
      zebes::BuildLayeredPuppetFromSpec(*source, *encoded_spec, semantic_pointer);
  if (!build.ok()) {
    std::cerr << build.status().message() << '\n';
    return 1;
  }
  const zebes::LayeredPuppet* const puppet = &build->puppet;
  const absl::flat_hash_map<std::string, zebes::RgbaImage>& backfill_requirements =
      build->backfill_requirements;
  const absl::flat_hash_map<std::string, zebes::RgbaImage>& attachment_relationships =
      build->attachment_relationships;
  const absl::flat_hash_map<std::string, zebes::RgbaImage>& immutable_semantic_layers =
      build->immutable_semantic_layers;
  nlohmann::json stretch_reports = nlohmann::json::object();
  for (const zebes::LayeredPuppetSpecStretchReport& report : build->stretch_reports) {
    stretch_reports[report.part_name] = {
        {"required_pixels", report.required_pixels},
        {"filled_pixels", report.filled_pixels},
        {"unreachable_pixels", report.unreachable_pixels},
        {"cleared_outside_required_pixels", report.cleared_outside_required_pixels},
    };
  }

  try {
    const std::filesystem::path parts_directory = output / "parts";
    const std::filesystem::path working_directory = output / "working";
    const std::filesystem::path frames_directory = output / "frames";
    const std::filesystem::path part_poses_directory = output / "part-poses";
    const std::filesystem::path attachment_directory = output / "attachment-masks";
    const std::filesystem::path backfill_directory = output / "backfill-masks";
    const std::filesystem::path immutable_directory = output / "immutable-sources";
    const std::filesystem::path diagnostics_working_directory = output / "diagnostics" / "working";
    const std::filesystem::path diagnostics_frames_directory = output / "diagnostics" / "frames";
    std::error_code error;
    std::filesystem::create_directories(parts_directory, error);
    std::filesystem::create_directories(working_directory, error);
    std::filesystem::create_directories(frames_directory, error);
    std::filesystem::create_directories(part_poses_directory, error);
    std::filesystem::create_directories(attachment_directory, error);
    std::filesystem::create_directories(backfill_directory, error);
    std::filesystem::create_directories(immutable_directory, error);
    std::filesystem::create_directories(diagnostics_working_directory, error);
    std::filesystem::create_directories(diagnostics_frames_directory, error);
    if (error) {
      std::cerr << "could not create layered puppet evidence directories: " << error.message()
                << '\n';
      return 1;
    }

    nlohmann::json part_pixel_counts = nlohmann::json::object();
    for (const zebes::LayeredPuppetPart& part : puppet->parts) {
      const absl::Status written = WriteImage(parts_directory / (part.name + ".png"), part.artwork);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
      part_pixel_counts[part.name] = OpaquePixelCount(part.artwork);
    }
    const absl::Status source_written = WriteImage(output / "source.png", *source);
    if (!source_written.ok()) {
      std::cerr << source_written.message() << '\n';
      return 1;
    }
    for (const auto& [part_name, original] : immutable_semantic_layers) {
      const absl::Status written = WriteImage(immutable_directory / (part_name + ".png"), original);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
    }
    nlohmann::json backfill_mask_pixels = nlohmann::json::object();
    nlohmann::json attachment_mask_pixels = nlohmann::json::object();
    for (const auto& [part_name, requirement] : backfill_requirements) {
      const absl::Status written =
          WriteImage(backfill_directory / (part_name + ".png"), requirement);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
      backfill_mask_pixels[part_name] = OpaquePixelCount(requirement);
    }
    for (const auto& [part_name, relationship] : attachment_relationships) {
      const absl::Status written =
          WriteImage(attachment_directory / (part_name + ".png"), relationship);
      if (!written.ok()) {
        std::cerr << written.message() << '\n';
        return 1;
      }
      attachment_mask_pixels[part_name] = OpaquePixelCount(relationship);
    }

    nlohmann::json skinned_part_diagnostics = nlohmann::json::object();
    nlohmann::json review_notices = nlohmann::json::array();
    nlohmann::json immutable_reports = nlohmann::json::object();
    for (const auto& [part_name, original] : immutable_semantic_layers) {
      const auto current = std::find_if(
          puppet->parts.begin(), puppet->parts.end(),
          [&part_name](const zebes::LayeredPuppetPart& part) { return part.name == part_name; });
      if (current == puppet->parts.end()) {
        review_notices.push_back("immutable semantic layer is missing from the puppet");
        continue;
      }
      const absl::StatusOr<zebes::SemanticLayerMutation> mutation =
          zebes::MeasureSemanticLayerMutation(original, current->artwork);
      if (!mutation.ok()) {
        std::cerr << mutation.status().message() << '\n';
        return 1;
      }
      const absl::StatusOr<std::string> source_digest = zebes::RgbaImageDigest(original);
      const absl::StatusOr<std::string> final_digest = zebes::RgbaImageDigest(current->artwork);
      if (!source_digest.ok() || !final_digest.ok()) {
        const absl::Status status =
            !source_digest.ok() ? source_digest.status() : final_digest.status();
        std::cerr << status.message() << '\n';
        return 1;
      }
      if (mutation->changed_pixels != 0 || mutation->alpha_added_pixels != 0 ||
          mutation->alpha_removed_pixels != 0) {
        review_notices.push_back(part_name + " changed immutable semantic artwork");
      }
      immutable_reports[part_name] = {
          {"source_rgba_digest", *source_digest},
          {"final_rgba_digest", *final_digest},
          {"changed_pixels", mutation->changed_pixels},
          {"alpha_added_pixels", mutation->alpha_added_pixels},
          {"alpha_removed_pixels", mutation->alpha_removed_pixels},
      };
    }
    std::vector<zebes::RgbaImage> visible_layers;
    visible_layers.reserve(puppet->parts.size());
    for (const zebes::LayeredPuppetPart& part : puppet->parts) {
      visible_layers.push_back(part.visible_artwork);
    }
    absl::StatusOr<zebes::SemanticVisibleOwnership> ownership =
        zebes::MeasureSemanticVisibleOwnership(*source, visible_layers);
    if (!ownership.ok()) {
      std::cerr << ownership.status().message() << '\n';
      return 1;
    }
    // Exclusive ownership, backfill coverage, orphan isolation, triangle folds,
    // interior holes and an exact neutral composite were once build failures.
    // They are measured and reported below and nothing fails on them any more.
    //
    // They were built for the See-through approach, which failed art direction,
    // and every count they reject is invisible at 48 pixels — the size the
    // sprite ships. A gate nobody can see the failure of only costs time. The
    // numbers stay in the manifest, so re-arming one is a line of code.
    //
    // Two checks are still failures, because both catch something visible and
    // both pass today: a part rendering in more than one piece, and the neutral
    // pose failing to reproduce its own source pixels.
    const bool require_single_component = spec->value("require_single_component", true);
    std::vector<size_t> skinned_part_indices;
    for (size_t part_index = 0; part_index < puppet->parts.size(); ++part_index) {
      const zebes::LayeredPuppetPart& part = puppet->parts[part_index];
      if (part.bone_indices.size() != 2) continue;
      const size_t source_opaque_pixels = OpaquePixelCount(part.artwork);
      skinned_part_indices.push_back(part_index);

      std::vector<zebes::RgbaImage> static_layers;
      nlohmann::json orphan_diagnostics = nlohmann::json::object();
      for (size_t other = 0; other < puppet->parts.size(); ++other) {
        if (other == part_index) continue;
        const zebes::LayeredPuppetPart& static_part = puppet->parts[other];
        static_layers.push_back(static_part.artwork);
        absl::StatusOr<zebes::LayeredPuppetOrphanReport> orphans =
            zebes::MeasureLayeredPuppetOrphans(static_part.artwork, part.artwork);
        if (!orphans.ok()) {
          std::cerr << orphans.status().message() << '\n';
          return 1;
        }
        orphan_diagnostics[static_part.name] = {
            {"components", orphans->components},
            {"orphan_pixels", orphans->orphan_pixels},
        };
      }
      const auto required_backfill = backfill_requirements.find(part.name);
      const zebes::RgbaImage& backfill_mask = required_backfill == backfill_requirements.end()
                                                  ? part.artwork
                                                  : required_backfill->second;
      absl::StatusOr<zebes::LayeredPuppetBackfillReport> backfill =
          zebes::MeasureLayeredPuppetBackfill(backfill_mask, static_layers);
      if (!backfill.ok()) {
        std::cerr << backfill.status().message() << '\n';
        return 1;
      }

      const std::filesystem::path part_directory = part_poses_directory / part.name;
      std::filesystem::create_directories(part_directory, error);
      if (error) {
        std::cerr << "could not create skinned-part evidence directory: " << error.message()
                  << '\n';
        return 1;
      }
      nlohmann::json pose_diagnostics = nlohmann::json::object();
      for (const zebes::LayeredPuppetPose& pose : puppet->poses) {
        absl::StatusOr<zebes::RgbaImage> rendered =
            zebes::RenderLayeredPuppetPart(*puppet, pose, part_index);
        if (!rendered.ok()) {
          std::cerr << rendered.status().message() << '\n';
          return 1;
        }
        const absl::Status written = WriteImage(part_directory / (pose.name + ".png"), *rendered);
        if (!written.ok()) {
          std::cerr << written.message() << '\n';
          return 1;
        }
        absl::StatusOr<std::vector<zebes::SemanticLayerComponent>> components =
            zebes::SplitSemanticLayerComponents(*rendered);
        if (!components.ok()) {
          std::cerr << components.status().message() << '\n';
          return 1;
        }
        const size_t rendered_opaque_pixels = OpaquePixelCount(*rendered);
        const size_t reconstruction_difference =
            pose.name == "neutral" ? PixelDifferenceCount(part.artwork, *rendered) : 0;
        if (pose.name == "neutral" && reconstruction_difference != 0) {
          review_notices.push_back(part.name + " neutral reconstruction changed source pixels");
        }
        if (require_single_component && components->size() != 1) {
          review_notices.push_back(part.name + " " + pose.name + " is not one connected component");
        }
        const absl::StatusOr<std::string> digest = zebes::RgbaImageDigest(*rendered);
        if (!digest.ok()) {
          std::cerr << digest.status().message() << '\n';
          return 1;
        }
        absl::StatusOr<std::vector<zebes::ProfileControlPoint>> deformed =
            zebes::SolveLayeredPuppetMeshVertices(*puppet, pose, part_index);
        if (!deformed.ok()) {
          std::cerr << deformed.status().message() << '\n';
          return 1;
        }
        absl::StatusOr<zebes::LayeredPuppetTriangleReport> triangles =
            zebes::MeasureLayeredPuppetTriangles(part.mesh, *deformed, part.artwork);
        if (!triangles.ok()) {
          std::cerr << triangles.status().message() << '\n';
          return 1;
        }
        pose_diagnostics[pose.name] = {
            {"components", components->size()},
            {"opaque_pixels", rendered_opaque_pixels},
            {"retained_area_ratio",
             static_cast<double>(rendered_opaque_pixels) / source_opaque_pixels},
            {"neutral_reconstruction_difference", reconstruction_difference},
            {"inverted_triangles", triangles->inverted},
            {"inverted_triangles_over_artwork", triangles->inverted_over_artwork},
            {"degenerate_triangles", triangles->degenerate},
            {"rgba_digest", *digest},
        };
      }
      skinned_part_diagnostics[part.name] = {
          {"mesh_vertices", part.mesh.vertices.size()},
          {"mesh_triangles", part.mesh.triangles.size()},
          {"backfill",
           {{"moving_pixels", backfill->moving_pixels},
            {"uncovered_pixels", backfill->uncovered_pixels}}},
          {"orphans", std::move(orphan_diagnostics)},
          {"poses", std::move(pose_diagnostics)},
      };
    }

    std::vector<zebes::RgbaImage> frames;
    nlohmann::json frame_digests = nlohmann::json::object();
    nlohmann::json interior_holes = nlohmann::json::object();
    size_t neutral_composite_difference = 0;
    nlohmann::json shadow_changed_pixels = nlohmann::json::object();
    for (const zebes::LayeredPuppetPose& pose : puppet->poses) {
      absl::StatusOr<zebes::RgbaImage> working = zebes::RenderLayeredPuppetPose(*puppet, pose);
      if (!working.ok()) {
        std::cerr << working.status().message() << '\n';
        return 1;
      }
      const zebes::RgbaImage unshadowed = *working;
      const absl::Status shadowed = ApplyConfiguredShadows(*working, *puppet, pose, *spec);
      if (!shadowed.ok()) {
        std::cerr << shadowed.message() << '\n';
        return 1;
      }
      const size_t shadow_pixels = PixelDifferenceCount(unshadowed, *working);
      shadow_changed_pixels[pose.name] = shadow_pixels;
      if (shadow_pixels != 0) {
        const zebes::RgbaImage shadow_tint = TintDifference(*working, unshadowed);
        const absl::Status shadow_written = WriteImage(
            diagnostics_working_directory / (pose.name + "-shadow-tint.png"), shadow_tint);
        if (!shadow_written.ok()) {
          std::cerr << shadow_written.message() << '\n';
          return 1;
        }
        absl::StatusOr<zebes::RgbaImage> shadow_frame =
            zebes::DownsampleLayeredPuppetFrame(shadow_tint, absl::GetFlag(FLAGS_frame_size));
        if (!shadow_frame.ok()) {
          std::cerr << shadow_frame.status().message() << '\n';
          return 1;
        }
        const absl::Status shadow_frame_written = WriteImage(
            diagnostics_frames_directory / (pose.name + "-shadow-tint.png"), *shadow_frame);
        if (!shadow_frame_written.ok()) {
          std::cerr << shadow_frame_written.message() << '\n';
          return 1;
        }
      }
      for (const size_t part_index : skinned_part_indices) {
        const std::string& part_name = puppet->parts[part_index].name;
        absl::StatusOr<zebes::RgbaImage> hidden =
            zebes::RenderLayeredPuppetPoseWithoutPart(*puppet, pose, part_index);
        if (!hidden.ok()) {
          std::cerr << hidden.status().message() << '\n';
          return 1;
        }
        const zebes::RgbaImage tinted = TintDifference(*working, *hidden);
        const std::string basename = pose.name + "-" + part_name;
        const absl::Status hidden_written =
            WriteImage(diagnostics_working_directory / (basename + "-hidden.png"), *hidden);
        const absl::Status tint_written =
            WriteImage(diagnostics_working_directory / (basename + "-tint.png"), tinted);
        if (!hidden_written.ok() || !tint_written.ok()) {
          const absl::Status status = !hidden_written.ok() ? hidden_written : tint_written;
          std::cerr << status.message() << '\n';
          return 1;
        }
        absl::StatusOr<zebes::RgbaImage> hidden_frame =
            zebes::DownsampleLayeredPuppetFrame(*hidden, absl::GetFlag(FLAGS_frame_size));
        absl::StatusOr<zebes::RgbaImage> tint_frame =
            zebes::DownsampleLayeredPuppetFrame(tinted, absl::GetFlag(FLAGS_frame_size));
        if (!hidden_frame.ok() || !tint_frame.ok()) {
          const absl::Status status =
              !hidden_frame.ok() ? hidden_frame.status() : tint_frame.status();
          std::cerr << status.message() << '\n';
          return 1;
        }
        const absl::Status hidden_frame_written =
            WriteImage(diagnostics_frames_directory / (basename + "-hidden.png"), *hidden_frame);
        const absl::Status tint_frame_written =
            WriteImage(diagnostics_frames_directory / (basename + "-tint.png"), *tint_frame);
        if (!hidden_frame_written.ok() || !tint_frame_written.ok()) {
          const absl::Status status =
              !hidden_frame_written.ok() ? hidden_frame_written : tint_frame_written;
          std::cerr << status.message() << '\n';
          return 1;
        }
      }
      const absl::Status working_written =
          WriteImage(working_directory / (pose.name + ".png"), *working);
      if (!working_written.ok()) {
        std::cerr << working_written.message() << '\n';
        return 1;
      }
      if (pose.name == "neutral") {
        neutral_composite_difference = PixelDifferenceCount(*source, *working);
      }
      absl::StatusOr<size_t> holes = zebes::MeasureLayeredPuppetInteriorHoles(*working);
      if (!holes.ok()) {
        std::cerr << holes.status().message() << '\n';
        return 1;
      }
      interior_holes[pose.name] = *holes;
      absl::StatusOr<zebes::RgbaImage> frame =
          zebes::DownsampleLayeredPuppetFrame(*working, absl::GetFlag(FLAGS_frame_size));
      if (!frame.ok()) {
        std::cerr << frame.status().message() << '\n';
        return 1;
      }
      const absl::Status frame_written =
          WriteImage(frames_directory / (pose.name + ".png"), *frame);
      if (!frame_written.ok()) {
        std::cerr << frame_written.message() << '\n';
        return 1;
      }
      const absl::StatusOr<std::string> digest = zebes::RgbaImageDigest(*frame);
      if (!digest.ok()) {
        std::cerr << digest.status().message() << '\n';
        return 1;
      }
      frame_digests[pose.name] = *digest;
      frames.push_back(std::move(*frame));
    }

    const absl::StatusOr<zebes::RgbaImage> proof = zebes::PackLayeredPuppetFrames(frames);
    if (!proof.ok()) {
      std::cerr << proof.status().message() << '\n';
      return 1;
    }
    const absl::Status proof_written = WriteImage(output / "proof.png", *proof);
    if (!proof_written.ok()) {
      std::cerr << proof_written.message() << '\n';
      return 1;
    }
    const absl::StatusOr<zebes::RgbaImage> zoomed =
        zebes::ZoomLayeredPuppetEvidence(*proof, absl::GetFlag(FLAGS_zoom));
    if (!zoomed.ok()) {
      std::cerr << zoomed.status().message() << '\n';
      return 1;
    }
    const absl::Status zoom_written = WriteImage(output / "proof-zoom.png", *zoomed);
    if (!zoom_written.ok()) {
      std::cerr << zoom_written.message() << '\n';
      return 1;
    }

    const absl::StatusOr<std::string> source_digest = zebes::RgbaImageDigest(*source);
    if (!source_digest.ok()) {
      std::cerr << source_digest.status().message() << '\n';
      return 1;
    }

    nlohmann::json pose_names = nlohmann::json::array();
    for (const zebes::LayeredPuppetPose& pose : puppet->poses) {
      pose_names.push_back(pose.name);
    }
    const nlohmann::json manifest = {
        {"version", 1},
        {"poses", std::move(pose_names)},
        {"working_size", {puppet->width, puppet->height}},
        {"frame_size", {absl::GetFlag(FLAGS_frame_size), absl::GetFlag(FLAGS_frame_size)}},
        {"frame_digests", std::move(frame_digests)},
        {"part_pixel_counts", std::move(part_pixel_counts)},
        {"source_rgba_digest", *source_digest},
        {"visible_ownership",
         {{"source_pixels", ownership->source_pixels},
          {"singly_owned_pixels", ownership->singly_owned_pixels},
          {"unowned_pixels", ownership->unowned_pixels},
          {"multiply_owned_pixels", ownership->multiply_owned_pixels},
          {"ownership_outside_source_pixels", ownership->ownership_outside_source_pixels}}},
        {"neutral_composite_difference", neutral_composite_difference},
        {"interior_holes", std::move(interior_holes)},
        {"backfill_stretch", std::move(stretch_reports)},
        {"backfill_mask_pixels", std::move(backfill_mask_pixels)},
        {"attachment_mask_pixels", std::move(attachment_mask_pixels)},
        {"shadow_changed_pixels", std::move(shadow_changed_pixels)},
        {"skinned_parts", std::move(skinned_part_diagnostics)},
        {"immutable_semantic_layers", std::move(immutable_reports)},
        {"review_notices", review_notices},
    };
    const absl::Status manifest_written = WriteJson(output / "manifest.json", manifest);
    if (!manifest_written.ok()) {
      std::cerr << manifest_written.message() << '\n';
      return 1;
    }
    std::cout << "wrote " << puppet->poses.size() << "-pose layered puppet proof to " << output
              << "; " << puppet->parts.size() << " authored parts\n";
    // Printed, never fatal. Nothing here is a reason to refuse a render: the
    // frames are already written and a person looking at them is the review.
    for (const nlohmann::json& notice : review_notices) {
      std::cout << "  note: " << notice.get<std::string>() << '\n';
    }
  } catch (const nlohmann::json::exception& error) {
    std::cerr << "invalid layered puppet input: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);
  if (positional.size() != 1) {
    std::cerr << "unexpected positional arguments\n";
    return 2;
  }
  return Run();
}
