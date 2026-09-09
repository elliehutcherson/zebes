#include "artwork/puppet_document_json.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "artwork/puppet_document.h"
#include "artwork/skeleton_rig.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "tests/macros.h"

namespace zebes {
namespace {

using puppet_edit::Command;

constexpr char kDocument[] = R"({
  "version": 1,
  "source_image": "mouse.png",
  "guide_image": "guides/biped.png",
  "width": 64,
  "height": 64,
  "require_single_component": true,
  "allow_overlap": true,
  "fps": 8,
  "skeleton_source": {"rig_path": "rig-bench.json", "clip_id": "run"},
  "anchor_frame": "run_01",
  "rest_pose": {
    "shoulder": [20, 20], "elbow": [30, 30], "wrist": [40, 40], "hip": [20, 50]
  },
  "joint_chains": {
    "shoulder": "spine", "elbow": "arm", "wrist": "arm", "hip": "spine"
  },
  "bones": [
    {"name": "upper_arm", "start_joint": "shoulder", "end_joint": "elbow", "may_stretch": false},
    {"name": "forearm", "start_joint": "elbow", "end_joint": "wrist", "may_stretch": true},
    {"name": "torso", "start_joint": "shoulder", "end_joint": "hip", "may_stretch": false}
  ],
  "parts": [
    {
      "name": "arm",
      "bones": ["upper_arm", "forearm"],
      "outlines": [[[10, 10], [30, 10], [30, 30], [10, 30]]],
      "exclude_outlines": [],
      "exclude_parts": [],
      "fills": [],
      "mesh_spacing": 4,
      "joint_blend_radius": 5.0,
      "joint_blend_lateral_scale": 0.5
    },
    {
      "name": "body",
      "bones": ["torso"],
      "outlines": [[[12, 12], [40, 12], [40, 50], [12, 50]]],
      "exclude_outlines": [[[14, 14], [20, 14], [20, 20]]],
      "exclude_parts": ["arm"],
      "fills": [{"polygon": [[15, 15], [25, 15], [25, 25]], "color": [10, 20, 30, 255]}],
      "mesh_spacing": 4,
      "joint_blend_radius": 12.0,
      "joint_blend_lateral_scale": 0.0
    }
  ],
  "frames": [
    {
      "name": "run_01",
      "pose": {"shoulder": [20, 20], "elbow": [30, 30], "wrist": [40, 40], "hip": [20, 50]},
      "draw_order": ["body", "arm"]
    },
    {
      "name": "run_02",
      "pose": {"shoulder": [20, 21], "elbow": [34, 30], "wrist": [44, 40], "hip": [20, 51]},
      "draw_order": ["body", "arm"]
    }
  ]
})";

// Returns kDocument with one exact substring swapped, so each failure test
// changes only the field it is about.
std::string DocumentWith(const std::string& original, const std::string& replacement) {
  std::string document = kDocument;
  const size_t position = document.find(original);
  EXPECT_NE(position, std::string::npos) << "test fixture no longer contains: " << original;
  document.replace(position, original.size(), replacement);
  return document;
}

TEST(PuppetDocumentJsonTest, ParsesEveryField) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);

  EXPECT_EQ(document->source_image, "mouse.png");
  EXPECT_EQ(document->guide_image, "guides/biped.png");
  EXPECT_EQ(document->width, 64);
  EXPECT_EQ(document->height, 64);
  EXPECT_EQ(document->skeleton_source.rig_path, "rig-bench.json");
  EXPECT_EQ(document->skeleton_source.clip_id, "run");
  EXPECT_EQ(document->anchor_frame, "run_01");
  EXPECT_EQ(document->rest_pose.size(), 4u);
  EXPECT_EQ(document->bones.size(), 3u);
  ASSERT_EQ(document->parts.size(), 2u);
  EXPECT_EQ(document->parts[0].joint_blend_lateral_scale, 0.5);
  ASSERT_EQ(document->frames.size(), 2u);
  EXPECT_EQ(document->frames[1].pose.at("elbow").x, 34);
}

TEST(PuppetDocumentJsonTest, RoundTripsWithoutDrift) {
  const absl::StatusOr<PuppetDocument> first = ParsePuppetDocument(kDocument);
  ASSERT_OK(first);
  const std::string encoded = PuppetDocumentToJson(*first);

  const absl::StatusOr<PuppetDocument> second = ParsePuppetDocument(encoded);
  ASSERT_OK(second);
  EXPECT_EQ(PuppetDocumentToJson(*second), encoded);
}

TEST(PuppetDocumentJsonTest, RejectsAnUnknownField) {
  const absl::StatusOr<PuppetDocument> document =
      ParsePuppetDocument(DocumentWith("\"width\": 64", "\"width\": 64, \"depth\": 3"));
  EXPECT_FALSE(document.ok());
}

TEST(PuppetDocumentJsonTest, RejectsAMissingField) {
  const absl::StatusOr<PuppetDocument> document =
      ParsePuppetDocument(DocumentWith("\"anchor_frame\": \"run_01\",", ""));
  EXPECT_FALSE(document.ok());
}

TEST(PuppetDocumentJsonTest, RejectsAnUnsupportedVersion) {
  const absl::StatusOr<PuppetDocument> document =
      ParsePuppetDocument(DocumentWith("\"version\": 1", "\"version\": 2"));
  EXPECT_FALSE(document.ok());
}

TEST(PuppetDocumentJsonTest, RejectsMalformedJsonWithoutThrowing) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument("{\"version\": 1,");
  ASSERT_FALSE(document.ok());
  EXPECT_EQ(document.status().code(), absl::StatusCode::kDataLoss);
}

TEST(PuppetDocumentJsonTest, RejectsADocumentThatFailsDocumentValidation) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(
      DocumentWith("\"anchor_frame\": \"run_01\"", "\"anchor_frame\": \"gone\""));
  EXPECT_FALSE(document.ok());
}

TEST(PuppetDocumentJsonTest, ParsesOwnershipAndFills) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);

  const PuppetDocumentPart& body = document->parts[1];
  ASSERT_EQ(body.exclude_outlines.size(), 1u);
  EXPECT_EQ(body.exclude_outlines.front().points.size(), 3u);
  EXPECT_EQ(body.exclude_parts, (std::vector<std::string>{"arm"}));
  ASSERT_EQ(body.fills.size(), 1u);
  EXPECT_EQ(body.fills.front().color, (std::array<uint8_t, 4>{10, 20, 30, 255}));
  EXPECT_TRUE(document->parts[0].exclude_parts.empty());
}

TEST(PuppetDocumentJsonTest, SpecJsonCarriesOwnershipAndFillsWithResolvedColor) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);
  const absl::StatusOr<std::string> encoded = PuppetDocumentSpecJson(*document);
  ASSERT_OK(encoded);

  const nlohmann::json body = nlohmann::json::parse(*encoded).at("parts")[1];
  EXPECT_EQ(body.at("source_exclude_polygons").size(), 1u);
  EXPECT_EQ(body.at("source_exclude_parts").get<std::vector<std::string>>(),
            (std::vector<std::string>{"arm"}));
  ASSERT_EQ(body.at("fill_polygons").size(), 1u);
  const nlohmann::json& fill = body.at("fill_polygons")[0];
  // The spec also accepts a point to sample; a document always resolves the
  // colour first, so a repainted source cannot change what the fill means.
  EXPECT_TRUE(fill.contains("color"));
  EXPECT_FALSE(fill.contains("sample"));
  EXPECT_EQ(fill.at("color").get<std::vector<int>>(), (std::vector<int>{10, 20, 30, 255}));
}

TEST(PuppetDocumentJsonTest, RejectsAFillColorOutsideRgba8) {
  const absl::StatusOr<PuppetDocument> document =
      ParsePuppetDocument(DocumentWith("[10, 20, 30, 255]", "[10, 20, 30, 999]"));
  EXPECT_FALSE(document.ok());
}

TEST(PuppetDocumentJsonTest, SpecJsonLeavesTheGuideImageBehind) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);
  const absl::StatusOr<std::string> encoded = PuppetDocumentSpecJson(*document);
  ASSERT_OK(encoded);

  EXPECT_EQ(encoded->find("guide_image"), std::string::npos);
  EXPECT_EQ(encoded->find("biped.png"), std::string::npos);
}

TEST(PuppetDocumentJsonTest, SpecJsonCarriesJointsBonesPartsAndFrames) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);
  const absl::StatusOr<std::string> encoded = PuppetDocumentSpecJson(*document);
  ASSERT_OK(encoded);

  const nlohmann::json spec = nlohmann::json::parse(*encoded);
  EXPECT_EQ(spec.at("version").get<int>(), 1);
  EXPECT_EQ(spec.at("width").get<int>(), 64);
  EXPECT_EQ(spec.at("joints").size(), 4u);
  EXPECT_EQ(spec.at("bones").size(), 3u);
  EXPECT_EQ(spec.at("pose_order").get<std::vector<std::string>>(),
            (std::vector<std::string>{"run_01", "run_02"}));
  EXPECT_EQ(spec.at("poses").size(), 2u);
  EXPECT_EQ(spec.at("draw_order").at("run_01").get<std::vector<std::string>>(),
            (std::vector<std::string>{"body", "arm"}));
  EXPECT_EQ(spec.at("editor_initial_pose").get<std::string>(), "run_01");
}

TEST(PuppetDocumentJsonTest, SpecJsonSpellsRigidAndSkinnedPartsDifferently) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);
  const absl::StatusOr<std::string> encoded = PuppetDocumentSpecJson(*document);
  ASSERT_OK(encoded);

  const nlohmann::json spec = nlohmann::json::parse(*encoded);
  const nlohmann::json& arm = spec.at("parts")[0];
  const nlohmann::json& body = spec.at("parts")[1];
  EXPECT_TRUE(arm.contains("bones"));
  EXPECT_FALSE(arm.contains("bone"));
  EXPECT_EQ(arm.at("source_polygons").size(), 1u);
  EXPECT_TRUE(body.contains("bone"));
  EXPECT_FALSE(body.contains("bones"));
  EXPECT_EQ(body.at("bone").get<std::string>(), "torso");
}

TEST(PuppetDocumentJsonTest, SpecJsonCarriesEachBonesStretchSetting) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);
  const absl::StatusOr<std::string> encoded = PuppetDocumentSpecJson(*document);
  ASSERT_OK(encoded);

  const nlohmann::json bones = nlohmann::json::parse(*encoded).at("bones");
  ASSERT_EQ(bones.size(), 3u);
  EXPECT_FALSE(bones[0].at("may_stretch").get<bool>());
  EXPECT_TRUE(bones[1].at("may_stretch").get<bool>());
}

// With overlap off, ownership is settled by declaration order, so the spec has
// every part giving up whatever the parts before it claimed.
TEST(PuppetDocumentJsonTest, SpecJsonExcludesEveryEarlierPartWhenOverlapIsOff) {
  const absl::StatusOr<PuppetDocument> allowed = ParsePuppetDocument(kDocument);
  ASSERT_OK(allowed);
  PuppetDocument refused = *allowed;
  ASSERT_OK(ApplyPuppetCommand(refused, puppet_edit::SetAllowOverlap{.allowed = false}));

  const absl::StatusOr<std::string> encoded = PuppetDocumentSpecJson(refused);
  ASSERT_OK(encoded);
  const nlohmann::json parts = nlohmann::json::parse(*encoded).at("parts");
  EXPECT_TRUE(parts[0].at("source_exclude_parts").empty());
  EXPECT_EQ(parts[1].at("source_exclude_parts").get<std::vector<std::string>>(),
            (std::vector<std::string>{"arm"}));

  // The outlines are untouched, so turning it back on restores the overlap.
  const absl::StatusOr<std::string> back = PuppetDocumentSpecJson(*allowed);
  ASSERT_OK(back);
  EXPECT_EQ(nlohmann::json::parse(*back).at("parts")[0].at("source_polygons"),
            parts[0].at("source_polygons"));
}

TEST(PuppetDocumentJsonTest, RigJsonCarriesJointsChainsBonesAndFrames) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);
  const absl::StatusOr<std::string> encoded = PuppetDocumentRigJson(*document, "run");
  ASSERT_OK(encoded);

  const nlohmann::json rig = nlohmann::json::parse(*encoded);
  EXPECT_EQ(rig.at("version"), 2);
  EXPECT_EQ(rig.at("points").size(), 4u);
  EXPECT_EQ(rig.at("bones").size(), 3u);
  EXPECT_EQ(rig.at("clips").at("run").at("fps"), 8);
  EXPECT_EQ(rig.at("clips").at("run").at("frames").size(), 2u);
  EXPECT_EQ(rig.at("clips").at("run").at("frames")[0].at("label"), "run_01");
  EXPECT_EQ(rig.at("clips").at("run").at("frames")[1].at("label"), "run_02");
  // The lowest joint, because a Rig Bench file needs a ground and a document
  // has none of its own.
  EXPECT_EQ(rig.at("floor_y"), 50.0);
  for (const nlohmann::json& point : rig.at("points")) {
    if (point.at("name") == "elbow") EXPECT_EQ(point.at("chain"), "arm");
  }
}

TEST(PuppetDocumentJsonTest, RigJsonReadsBackAsARig) {
  const absl::StatusOr<PuppetDocument> document = ParsePuppetDocument(kDocument);
  ASSERT_OK(document);
  const absl::StatusOr<std::string> encoded = PuppetDocumentRigJson(*document, "run");
  ASSERT_OK(encoded);

  const absl::StatusOr<SkeletonRig> rig = ParseSkeletonRig(*encoded);
  ASSERT_OK(rig);
  EXPECT_EQ(rig->points.size(), 4u);
  EXPECT_EQ(rig->clips.size(), 1u);
}

// The case a rig authored on its own is in: joints and bones, no artwork and no
// frames. The rest pose becomes the clip, which is where an import reads one.
TEST(PuppetDocumentJsonTest, RigJsonFromASkeletonWithNoArtworkOrFrames) {
  PuppetDocument document;
  ASSERT_TRUE(ApplyPuppetCommand(
                  document,
                  puppet_edit::AddJoint{.name = "hip", .rest = {.x = 5, .y = 9}, .chain = "spine"})
                  .ok());
  ASSERT_TRUE(ApplyPuppetCommand(
                  document,
                  puppet_edit::AddJoint{.name = "knee", .rest = {.x = 6, .y = 20}, .chain = "leg"})
                  .ok());
  ASSERT_TRUE(ApplyPuppetCommand(
                  document,
                  puppet_edit::AddBone{.name = "thigh", .start_joint = "hip", .end_joint = "knee"})
                  .ok());
  ASSERT_FALSE(PuppetDocumentSpecJson(document).ok());

  const absl::StatusOr<std::string> encoded = PuppetDocumentRigJson(document, "authored");
  ASSERT_OK(encoded);
  const nlohmann::json rig = nlohmann::json::parse(*encoded);
  const nlohmann::json& frames = rig.at("clips").at("authored").at("frames");
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].at("label"), "rest");
  EXPECT_EQ(frames[0].at("pose").at("knee")[1], 20.0);
}

TEST(PuppetDocumentJsonTest, RigJsonRefusesASkeletonWithNoBones) {
  PuppetDocument document;
  EXPECT_EQ(PuppetDocumentRigJson(document, "authored").status().code(),
            absl::StatusCode::kFailedPrecondition);
  const absl::StatusOr<PuppetDocument> whole = ParsePuppetDocument(kDocument);
  ASSERT_OK(whole);
  EXPECT_EQ(PuppetDocumentRigJson(*whole, "").status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(PuppetDocumentJsonTest, SpecJsonRefusesAnUnfinishedDocument) {
  PuppetDocument document;
  const absl::StatusOr<std::string> encoded = PuppetDocumentSpecJson(document);
  ASSERT_FALSE(encoded.ok());
  EXPECT_EQ(encoded.status().message(), "choose a source image");
}

// Every shipped document is loaded here, so a format change that a migration
// missed fails a test instead of failing the first person to open the editor.
//
// Loading is the whole bar. A document half way through being authored is a
// normal thing to have on disk, so one that cannot build yet is only required
// to say what it still needs.
TEST(PuppetDocumentJsonTest, LoadsEveryTrackedDocument) {
  const std::filesystem::path directory =
      std::filesystem::path(ZEBES_SOURCE_DIR) / "experiments/character_binding/puppet_documents";
  ASSERT_TRUE(std::filesystem::is_directory(directory)) << directory;

  size_t loaded = 0;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() != ".json") continue;
    const absl::StatusOr<PuppetDocument> document = LoadPuppetDocument(entry.path());
    ASSERT_OK(document) << ": " << entry.path();
    const absl::Status ready = PuppetDocumentReadyToBuild(*document);
    if (ready.ok()) {
      EXPECT_OK(PuppetDocumentSpecJson(*document)) << ": " << entry.path();
    } else {
      EXPECT_FALSE(ready.message().empty()) << entry.path();
    }
    ++loaded;
  }
  EXPECT_GT(loaded, 0u);
}

TEST(PuppetDocumentJsonTest, ParsesEveryCommandKind) {
  constexpr char kCommands[] = R"([
    {"command": "set_source_image", "path": "a.png", "width": 32, "height": 32},
    {"command": "scale_to_size", "width": 64, "height": 64},
    {"command": "set_guide_image", "path": "guides/biped.png"},
    {"command": "add_joint", "name": "hip", "rest": [1, 2], "chain": "spine"},
    {"command": "set_joint_chain", "name": "hip", "chain": "pelvis"},
    {"command": "set_frame_rate", "fps": 12},
    {"command": "set_allow_overlap", "allowed": false},
    {"command": "set_bone_stretch", "name": "spine", "may_stretch": true},
    {"command": "move_rest_joint", "name": "hip", "rest": [3, 4]},
    {"command": "add_bone", "name": "spine", "start_joint": "hip", "end_joint": "neck"},
    {"command": "remove_bone", "name": "spine"},
    {"command": "remove_joint", "name": "hip"},
    {"command": "add_frames", "name_prefix": "run", "count": 12, "copy_from": ""},
    {"command": "remove_frame", "name": "run_01"},
    {"command": "reorder_frames", "order": ["run_02", "run_01"]},
    {"command": "rename_frame", "name": "run_02", "new_name": "contact"},
    {"command": "rebase_frames", "from_frame": "run_02"},
    {"command": "retarget_frames", "from_frame": "run_02"},
    {"command": "fit_bone_lengths", "frame": "run_02"},
    {"command": "set_anchor_frame", "name": "run_02"},
    {"command": "pose_joint", "frame": "run_02", "joint": "hip", "point": [5, 6],
     "scope": "all_frames"},
    {"command": "add_part", "name": "arm", "bones": ["a", "b"]},
    {"command": "set_part_bones", "part": "arm", "bones": ["c"]},
    {"command": "set_part_outline", "part": "arm", "outlines": [[[0, 0], [1, 0], [1, 1]]]},
    {"command": "set_part_exclude_outlines", "part": "arm", "outlines": [[[0, 0], [1, 0], [1, 1]]]},
    {"command": "set_part_exclude_parts", "part": "arm", "excluded": ["body"]},
    {"command": "set_part_fills", "part": "arm",
     "fills": [{"polygon": [[0, 0], [1, 0], [1, 1]], "color": [1, 2, 3, 4]}]},
    {"command": "set_part_mesh", "part": "arm", "spacing": 4, "joint_blend_radius": 5.0,
     "joint_blend_lateral_scale": 0.5},
    {"command": "set_draw_order", "frame": "run_02", "order": ["arm"]},
    {"command": "rename_part", "name": "arm", "new_name": "forearm_l"},
    {"command": "remove_part", "name": "arm"}
  ])";

  const absl::StatusOr<std::vector<Command>> commands = ParsePuppetCommands(kCommands, ".");
  ASSERT_OK(commands);
  EXPECT_EQ(commands->size(), 31u);
}

TEST(PuppetDocumentJsonTest, PoseJointScopeIsReadFromTheCommand) {
  const absl::StatusOr<std::vector<Command>> commands = ParsePuppetCommands(
      R"([{"command": "pose_joint", "frame": "f", "joint": "j", "point": [1, 2],
           "scope": "frame"}])",
      ".");
  ASSERT_OK(commands);
  ASSERT_EQ(commands->size(), 1u);
  const auto* const posed = std::get_if<puppet_edit::PoseJoint>(&commands->front());
  ASSERT_NE(posed, nullptr);
  EXPECT_EQ(posed->scope, PoseJointScope::kFrame);
}

TEST(PuppetDocumentJsonTest, RejectsAnUnknownCommand) {
  const absl::StatusOr<std::vector<Command>> commands =
      ParsePuppetCommands(R"([{"command": "explode"}])", ".");
  EXPECT_FALSE(commands.ok());
}

TEST(PuppetDocumentJsonTest, RejectsAnUnknownFieldOnACommand) {
  const absl::StatusOr<std::vector<Command>> commands =
      ParsePuppetCommands(R"([{"command": "remove_frame", "name": "a", "force": true}])", ".");
  EXPECT_FALSE(commands.ok());
}

TEST(PuppetDocumentJsonTest, RejectsAMissingFieldOnACommand) {
  const absl::StatusOr<std::vector<Command>> commands =
      ParsePuppetCommands(R"([{"command": "add_frames", "name_prefix": "run", "count": 2}])", ".");
  EXPECT_FALSE(commands.ok());
}

TEST(PuppetDocumentJsonTest, RejectsAnUnknownPoseScope) {
  const absl::StatusOr<std::vector<Command>> commands = ParsePuppetCommands(
      R"([{"command": "pose_joint", "frame": "f", "joint": "j", "point": [1, 2],
           "scope": "everything"}])",
      ".");
  EXPECT_FALSE(commands.ok());
}

TEST(PuppetDocumentJsonTest, RejectsACommandListThatIsNotAnArray) {
  EXPECT_FALSE(ParsePuppetCommands(R"({"command": "remove_frame", "name": "a"})", ".").ok());
}

TEST(PuppetDocumentJsonTest, RejectsAnImportWhoseRigFileIsMissing) {
  const absl::StatusOr<std::vector<Command>> commands = ParsePuppetCommands(
      R"([{"command": "import_skeleton", "rig_path": "absent.json", "clip_id": "run",
           "import_frames": true}])",
      ".");
  EXPECT_FALSE(commands.ok());
}

}  // namespace
}  // namespace zebes
