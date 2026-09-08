#include "artwork/puppet_document.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "artwork/layered_puppet.h"
#include "artwork/skeleton_rig.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

using puppet_edit::AddBone;
using puppet_edit::AddFrames;
using puppet_edit::AddJoint;
using puppet_edit::AddPart;
using puppet_edit::Command;
using puppet_edit::ImportSkeleton;
using puppet_edit::MoveRestJoint;
using puppet_edit::PoseJoint;
using puppet_edit::RemoveBone;
using puppet_edit::RemoveFrame;
using puppet_edit::RemoveJoint;
using puppet_edit::RemovePart;
using puppet_edit::SetAnchorFrame;
using puppet_edit::SetDrawOrder;
using puppet_edit::SetPartMesh;
using puppet_edit::SetPartOutline;
using puppet_edit::SetSourceImage;

void ApplyOrDie(PuppetDocument& document, const Command& command) {
  ASSERT_OK(ApplyPuppetCommand(document, command));
}

// A two-bone arm on a torso, enough to exercise skinned and rigid parts.
PuppetDocument SkeletonDocument() {
  PuppetDocument document;
  ApplyOrDie(document, SetSourceImage{.path = "mouse.png", .width = 64, .height = 64});
  ApplyOrDie(document, AddJoint{.name = "shoulder", .rest = {.x = 20, .y = 20}, .chain = "spine"});
  ApplyOrDie(document, AddJoint{.name = "elbow", .rest = {.x = 30, .y = 30}, .chain = "arm"});
  ApplyOrDie(document, AddJoint{.name = "wrist", .rest = {.x = 40, .y = 40}, .chain = "arm"});
  ApplyOrDie(document, AddJoint{.name = "hip", .rest = {.x = 20, .y = 50}, .chain = "spine"});
  ApplyOrDie(document,
             AddBone{.name = "upper_arm", .start_joint = "shoulder", .end_joint = "elbow"});
  ApplyOrDie(document, AddBone{.name = "forearm", .start_joint = "elbow", .end_joint = "wrist"});
  ApplyOrDie(document, AddBone{.name = "torso", .start_joint = "shoulder", .end_joint = "hip"});
  return document;
}

std::vector<LayeredPuppetPolygon> Square() {
  return {LayeredPuppetPolygon{
      .points = {{.x = 10, .y = 10}, {.x = 30, .y = 10}, {.x = 30, .y = 30}, {.x = 10, .y = 30}}}};
}

PuppetDocument BuildableDocument() {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddPart{.name = "arm", .bones = {"upper_arm", "forearm"}});
  ApplyOrDie(document, AddPart{.name = "body", .bones = {"torso"}});
  ApplyOrDie(document, SetPartOutline{.part = "arm", .outlines = Square()});
  ApplyOrDie(document, SetPartOutline{.part = "body", .outlines = Square()});
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 3});
  return document;
}

SkeletonRig TwoFrameRig() {
  SkeletonRig rig;
  rig.version = 2;
  rig.points = {{.name = "hip", .chain = "spine"},
                {.name = "knee", .chain = "leg_l"},
                {.name = "ankle", .chain = "leg_l"}};
  rig.bones = {{.start = "hip", .end = "knee"}, {.start = "knee", .end = "ankle"}};
  SkeletonRigClip clip{.id = "run", .name = "run", .fps = 8};
  clip.frames.push_back({.label = "contact",
                         .pose = {{"hip", {.x = 10, .y = 10}},
                                  {"knee", {.x = 12, .y = 20}},
                                  {"ankle", {.x = 14, .y = 30}}}});
  clip.frames.push_back({.label = "passing",
                         .pose = {{"hip", {.x = 10, .y = 12}},
                                  {"knee", {.x = 18, .y = 22}},
                                  {"ankle", {.x = 22, .y = 32}}}});
  rig.clips.push_back(std::move(clip));
  return rig;
}

TEST(PuppetDocumentTest, GuideImageIsSetClearedAndNeverRequiredToBuild) {
  PuppetDocument document = BuildableDocument();
  EXPECT_TRUE(document.guide_image.empty());
  EXPECT_OK(PuppetDocumentReadyToBuild(document));

  ApplyOrDie(document, puppet_edit::SetGuideImage{.path = "guides/biped.png"});
  EXPECT_EQ(document.guide_image, "guides/biped.png");
  EXPECT_OK(PuppetDocumentReadyToBuild(document));

  ApplyOrDie(document, puppet_edit::SetGuideImage{.path = ""});
  EXPECT_TRUE(document.guide_image.empty());
}

TEST(PuppetDocumentTest, GuideImageIsIndependentOfTheSourceImage) {
  PuppetDocument document;
  ApplyOrDie(document, puppet_edit::SetGuideImage{.path = "guides/biped.png"});
  EXPECT_EQ(PuppetDocumentReadyToBuild(document).message(), "choose a source image");

  ApplyOrDie(document, SetSourceImage{.path = "mouse.png", .width = 64, .height = 64});
  EXPECT_EQ(document.guide_image, "guides/biped.png");
  EXPECT_EQ(document.source_image, "mouse.png");
}

TEST(PuppetDocumentTest, EmptyDocumentIsValidButNotBuildable) {
  const PuppetDocument document;
  EXPECT_OK(ValidatePuppetDocument(document));
  const absl::Status ready = PuppetDocumentReadyToBuild(document);
  EXPECT_FALSE(ready.ok());
  EXPECT_EQ(ready.message(), "choose a source image");
}

TEST(PuppetDocumentTest, ReadinessNamesTheNextStepInAuthoringOrder) {
  PuppetDocument document;
  ApplyOrDie(document, SetSourceImage{.path = "mouse.png", .width = 64, .height = 64});
  EXPECT_EQ(PuppetDocumentReadyToBuild(document).message(), "import or draw a skeleton");

  document = SkeletonDocument();
  EXPECT_EQ(PuppetDocumentReadyToBuild(document).message(), "add at least one part");

  ApplyOrDie(document, AddPart{.name = "arm", .bones = {"upper_arm", "forearm"}});
  EXPECT_EQ(PuppetDocumentReadyToBuild(document).message(), "outline or fill part 'arm'");

  ApplyOrDie(document, SetPartOutline{.part = "arm", .outlines = Square()});
  EXPECT_EQ(PuppetDocumentReadyToBuild(document).message(), "add at least one frame");

  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 1});
  EXPECT_OK(PuppetDocumentReadyToBuild(document));
}

TEST(PuppetDocumentTest, AddFramesCopiesTheRestPoseAndNamesFramesInOrder) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 12});

  ASSERT_EQ(document.frames.size(), 12u);
  EXPECT_EQ(document.frames.front().name, "run_01");
  EXPECT_EQ(document.frames.back().name, "run_12");
  EXPECT_EQ(document.anchor_frame, "run_01");
  for (const PuppetDocumentFrame& frame : document.frames) {
    ASSERT_EQ(frame.pose.size(), document.rest_pose.size());
    for (const auto& [name, rest] : document.rest_pose) {
      EXPECT_EQ(frame.pose.at(name).x, rest.x);
      EXPECT_EQ(frame.pose.at(name).y, rest.y);
    }
  }
}

TEST(PuppetDocumentTest, AddFramesSkipsNamesAlreadyTaken) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 2});
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 2});

  ASSERT_EQ(document.frames.size(), 4u);
  EXPECT_EQ(document.frames[2].name, "run_03");
  EXPECT_EQ(document.frames[3].name, "run_04");
}

TEST(PuppetDocumentTest, AddFramesCopiesTheNamedFrameInsteadOfTheRestPose) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 1});
  ApplyOrDie(document, PoseJoint{.frame = "run_01", .joint = "wrist", .point = {.x = 60, .y = 5}});
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 1, .copy_from = "run_01"});

  ASSERT_EQ(document.frames.size(), 2u);
  EXPECT_EQ(document.frames[1].pose.at("wrist").x, 60);
  EXPECT_EQ(document.frames[1].pose.at("wrist").y, 5);
}

TEST(PuppetDocumentTest, AddFramesRejectsAnUnknownSourceFrame) {
  PuppetDocument document = SkeletonDocument();
  const absl::Status applied = ApplyPuppetCommand(
      document, AddFrames{.name_prefix = "run", .count = 1, .copy_from = "gone"});
  EXPECT_EQ(applied.code(), absl::StatusCode::kNotFound);
  EXPECT_TRUE(document.frames.empty());
}

TEST(PuppetDocumentTest, PosingOneFrameLeavesTheOthersAlone) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 3});
  ApplyOrDie(document, PoseJoint{.frame = "run_02",
                                 .joint = "elbow",
                                 .point = {.x = 50, .y = 30},
                                 .scope = PoseJointScope::kFrame});

  EXPECT_EQ(document.frames[0].pose.at("elbow").x, 30);
  EXPECT_EQ(document.frames[1].pose.at("elbow").x, 50);
  EXPECT_EQ(document.frames[2].pose.at("elbow").x, 30);
}

TEST(PuppetDocumentTest, PosingEveryFrameShiftsThemAllAndKeepsTheirDifferences) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 3});
  // Give each frame its own elbow so the shift has a relationship to preserve.
  ApplyOrDie(document, PoseJoint{.frame = "run_01", .joint = "elbow", .point = {.x = 30, .y = 30}});
  ApplyOrDie(document, PoseJoint{.frame = "run_02", .joint = "elbow", .point = {.x = 34, .y = 30}});
  ApplyOrDie(document, PoseJoint{.frame = "run_03", .joint = "elbow", .point = {.x = 42, .y = 30}});

  ApplyOrDie(document, PoseJoint{.frame = "run_02",
                                 .joint = "elbow",
                                 .point = {.x = 44, .y = 30},
                                 .scope = PoseJointScope::kAllFrames});

  EXPECT_EQ(document.frames[0].pose.at("elbow").x, 40);
  EXPECT_EQ(document.frames[1].pose.at("elbow").x, 44);
  EXPECT_EQ(document.frames[2].pose.at("elbow").x, 52);
}

TEST(PuppetDocumentTest, PoseJointRejectsUnknownFrameAndJoint) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 1});
  EXPECT_EQ(ApplyPuppetCommand(document, PoseJoint{.frame = "nope", .joint = "elbow", .point = {}})
                .code(),
            absl::StatusCode::kNotFound);
  EXPECT_EQ(ApplyPuppetCommand(document, PoseJoint{.frame = "run_01", .joint = "tail", .point = {}})
                .code(),
            absl::StatusCode::kNotFound);
}

TEST(PuppetDocumentTest, AddingAJointPosesItInEveryExistingFrame) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 2});
  ApplyOrDie(document, AddJoint{.name = "tail", .rest = {.x = 5, .y = 55}, .chain = "tail"});

  for (const PuppetDocumentFrame& frame : document.frames) {
    EXPECT_EQ(frame.pose.at("tail").x, 5);
  }
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, EveryJointCarriesAChainAndAnEmptyOneIsRefused) {
  PuppetDocument document = SkeletonDocument();
  EXPECT_EQ(document.joint_chains.at("elbow"), "arm");
  EXPECT_EQ(document.joint_chains.size(), document.rest_pose.size());

  EXPECT_EQ(
      ApplyPuppetCommand(document, AddJoint{.name = "toe", .rest = {.x = 1, .y = 1}, .chain = ""})
          .code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(document.rest_pose.contains("toe"));
}

TEST(PuppetDocumentTest, SetJointChainMovesAJointToAnotherLimbAndNotTheJoint) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, puppet_edit::SetJointChain{.name = "wrist", .chain = "hand"});
  EXPECT_EQ(document.joint_chains.at("wrist"), "hand");
  EXPECT_DOUBLE_EQ(document.rest_pose.at("wrist").x, 40);

  EXPECT_EQ(
      ApplyPuppetCommand(document, puppet_edit::SetJointChain{.name = "ghost", .chain = "arm"})
          .code(),
      absl::StatusCode::kNotFound);
  EXPECT_EQ(
      ApplyPuppetCommand(document, puppet_edit::SetJointChain{.name = "wrist", .chain = ""}).code(),
      absl::StatusCode::kInvalidArgument);
}

TEST(PuppetDocumentTest, ADocumentWithAChainlessJointFailsValidation) {
  PuppetDocument document = SkeletonDocument();
  document.joint_chains.erase("elbow");
  EXPECT_FALSE(ValidatePuppetDocument(document).ok());
}

TEST(PuppetDocumentTest, OverlapIsAllowedUntilItIsTurnedOff) {
  PuppetDocument document = BuildableDocument();
  EXPECT_TRUE(document.allow_overlap);
  ApplyOrDie(document, puppet_edit::SetAllowOverlap{.allowed = false});
  EXPECT_FALSE(document.allow_overlap);
  // Nothing traced is thrown away, so turning it back on restores the overlap.
  EXPECT_EQ(document.parts[0].outlines.size(), 1u);
  EXPECT_TRUE(document.parts[0].exclude_parts.empty());
}

TEST(PuppetDocumentTest, BonesAreRigidUntilOneIsToldItMayStretch) {
  PuppetDocument document = SkeletonDocument();
  for (const PuppetDocumentBone& bone : document.bones) EXPECT_FALSE(bone.may_stretch);

  ApplyOrDie(document, puppet_edit::SetBoneStretch{.name = "forearm", .may_stretch = true});
  EXPECT_TRUE(document.bones[1].may_stretch);
  EXPECT_FALSE(document.bones[0].may_stretch);

  EXPECT_EQ(ApplyPuppetCommand(document,
                               puppet_edit::SetBoneStretch{.name = "ghost", .may_stretch = true})
                .code(),
            absl::StatusCode::kNotFound);
}

TEST(PuppetDocumentTest, FrameRateIsStoredAndBounded) {
  PuppetDocument document = SkeletonDocument();
  EXPECT_EQ(document.fps, 8);
  ApplyOrDie(document, puppet_edit::SetFrameRate{.fps = 24});
  EXPECT_EQ(document.fps, 24);

  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::SetFrameRate{.fps = 0}).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::SetFrameRate{.fps = 1000}).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(document.fps, 24);
}

TEST(PuppetDocumentTest, ImportSkeletonTakesChainsAndTheClipFrameRate) {
  PuppetDocument document;
  ApplyOrDie(document, ImportSkeleton{.rig = TwoFrameRig(), .clip_id = "run"});
  EXPECT_EQ(document.joint_chains.at("hip"), "spine");
  EXPECT_EQ(document.joint_chains.at("ankle"), "leg_l");
  EXPECT_EQ(document.fps, 8);
}

TEST(PuppetDocumentTest, RemovingAJointClearsItFromEveryFrame) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddJoint{.name = "tail", .rest = {.x = 5, .y = 55}, .chain = "tail"});
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 2});
  ApplyOrDie(document, RemoveJoint{.name = "tail"});

  for (const PuppetDocumentFrame& frame : document.frames) {
    EXPECT_FALSE(frame.pose.contains("tail"));
  }
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, RemovingAJointAndBoneStillInUseIsRefused) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, RemoveJoint{.name = "elbow"}).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(ApplyPuppetCommand(document, RemoveBone{.name = "forearm"}).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(document.rest_pose.contains("elbow"));
}

TEST(PuppetDocumentTest, DuplicateJointBonePartAreRefused) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(
      ApplyPuppetCommand(document, AddJoint{.name = "elbow", .rest = {}, .chain = "arm"}).code(),
      absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(ApplyPuppetCommand(
                document, AddBone{.name = "forearm", .start_joint = "shoulder", .end_joint = "hip"})
                .code(),
            absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(ApplyPuppetCommand(document, AddPart{.name = "arm", .bones = {"torso"}}).code(),
            absl::StatusCode::kAlreadyExists);
}

TEST(PuppetDocumentTest, MoveRestJointDoesNotDisturbFrames) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 2});
  ApplyOrDie(document, MoveRestJoint{.name = "elbow", .rest = {.x = 1, .y = 2}});

  EXPECT_EQ(document.rest_pose.at("elbow").x, 1);
  EXPECT_EQ(document.frames[0].pose.at("elbow").x, 30);
}

TEST(PuppetDocumentTest, AddingAPartAppendsItToEveryFrameDrawOrder) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddFrames{.name_prefix = "run", .count = 2});
  ApplyOrDie(document, AddPart{.name = "arm", .bones = {"upper_arm", "forearm"}});
  ApplyOrDie(document, AddPart{.name = "body", .bones = {"torso"}});

  for (const PuppetDocumentFrame& frame : document.frames) {
    EXPECT_EQ(frame.draw_order, (std::vector<std::string>{"arm", "body"}));
  }
}

TEST(PuppetDocumentTest, RenamingAPartCarriesDrawOrdersAndExcludesWithIt) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, puppet_edit::SetPartExcludeParts{.part = "body", .excluded = {"arm"}});
  ApplyOrDie(document, puppet_edit::RenamePart{.name = "arm", .new_name = "forearm_l"});

  EXPECT_EQ(document.parts[0].name, "forearm_l");
  EXPECT_EQ(document.parts[1].exclude_parts, (std::vector<std::string>{"forearm_l"}));
  for (const PuppetDocumentFrame& frame : document.frames) {
    EXPECT_EQ(frame.draw_order, (std::vector<std::string>{"forearm_l", "body"}));
  }
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, RenamingAPartKeepsItsOutlineAndMeshSettings) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, SetPartMesh{.part = "arm", .spacing = 7, .joint_blend_radius = 3});
  ApplyOrDie(document, puppet_edit::RenamePart{.name = "arm", .new_name = "forearm_l"});

  EXPECT_EQ(document.parts[0].outlines.size(), 1u);
  EXPECT_EQ(document.parts[0].mesh_spacing, 7);
  EXPECT_EQ(document.parts[0].joint_blend_radius, 3);
  EXPECT_EQ(document.parts[0].bones, (std::vector<std::string>{"upper_arm", "forearm"}));
}

TEST(PuppetDocumentTest, RenamingAPartRejectsUnknownTakenAndUnusableNames) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::RenamePart{.name = "ghost", .new_name = "a"})
                .code(),
            absl::StatusCode::kNotFound);
  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::RenamePart{.name = "arm", .new_name = "body"})
                .code(),
            absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(
      ApplyPuppetCommand(document, puppet_edit::RenamePart{.name = "arm", .new_name = "../evil"})
          .code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(document.parts[0].name, "arm");
}

TEST(PuppetDocumentTest, RenamingAPartToItsOwnNameIsAllowed) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, puppet_edit::RenamePart{.name = "arm", .new_name = "arm"});
  EXPECT_EQ(document.parts[0].name, "arm");
}

TEST(PuppetDocumentTest, RenamingAPartLeavesItOnTheSameBones) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, puppet_edit::RenamePart{.name = "body", .new_name = "arm_v2"});
  EXPECT_EQ(document.parts[1].bones, (std::vector<std::string>{"torso"}));
}

TEST(PuppetDocumentTest, SetPartBonesRepointsAPartAndKeepsItsOutline) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, SetPartMesh{.part = "arm", .spacing = 7, .joint_blend_radius = 3});
  ApplyOrDie(document, puppet_edit::SetPartBones{.part = "arm", .bones = {"torso"}});

  EXPECT_EQ(document.parts[0].bones, (std::vector<std::string>{"torso"}));
  EXPECT_EQ(document.parts[0].outlines.size(), 1u);
  EXPECT_EQ(document.parts[0].mesh_spacing, 7);
  EXPECT_EQ(document.parts[0].joint_blend_radius, 3);
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, SetPartBonesRejectsUnknownPartsAndUnusableBones) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(
      ApplyPuppetCommand(document, puppet_edit::SetPartBones{.part = "ghost", .bones = {"torso"}})
          .code(),
      absl::StatusCode::kNotFound);
  EXPECT_EQ(
      ApplyPuppetCommand(document, puppet_edit::SetPartBones{.part = "arm", .bones = {"missing"}})
          .code(),
      absl::StatusCode::kInvalidArgument);
  // upper_arm and torso meet at the shoulder; forearm and torso do not.
  EXPECT_EQ(ApplyPuppetCommand(
                document, puppet_edit::SetPartBones{.part = "arm", .bones = {"forearm", "torso"}})
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      ApplyPuppetCommand(document, puppet_edit::SetPartBones{.part = "arm", .bones = {}}).code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(document.parts[0].bones, (std::vector<std::string>{"upper_arm", "forearm"}));
}

TEST(PuppetDocumentTest, ScaleToSizeStretchesJointsFramesOutlinesAndFills) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document,
             puppet_edit::SetPartFills{
                 .part = "arm", .fills = {{.polygon = Square().front(), .color = {1, 2, 3, 255}}}});
  ApplyOrDie(document, PoseJoint{.frame = "run_01", .joint = "elbow", .point = {.x = 33, .y = 11}});
  ApplyOrDie(document, puppet_edit::ScaleToSize{.width = 128, .height = 256});

  EXPECT_EQ(document.width, 128);
  EXPECT_EQ(document.height, 256);
  EXPECT_DOUBLE_EQ(document.rest_pose.at("elbow").x, 60);
  EXPECT_DOUBLE_EQ(document.rest_pose.at("elbow").y, 120);
  EXPECT_DOUBLE_EQ(document.frames[0].pose.at("elbow").x, 66);
  EXPECT_DOUBLE_EQ(document.frames[0].pose.at("elbow").y, 44);
  EXPECT_DOUBLE_EQ(document.parts[0].outlines[0].points[1].x, 60);
  EXPECT_DOUBLE_EQ(document.parts[0].outlines[0].points[1].y, 40);
  EXPECT_DOUBLE_EQ(document.parts[0].fills[0].polygon.points[1].x, 60);
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, ScaleToSizeCarriesMeshDistancesButNotTheLateralRatio) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(
      document,
      SetPartMesh{
          .part = "arm", .spacing = 4, .joint_blend_radius = 12, .joint_blend_lateral_scale = 0.5});
  ApplyOrDie(document, puppet_edit::ScaleToSize{.width = 128, .height = 128});

  EXPECT_EQ(document.parts[0].mesh_spacing, 8);
  EXPECT_DOUBLE_EQ(document.parts[0].joint_blend_radius, 24);
  EXPECT_DOUBLE_EQ(document.parts[0].joint_blend_lateral_scale, 0.5);
}

TEST(PuppetDocumentTest, ScaleToSizeNeedsASizeToScaleFrom) {
  PuppetDocument empty;
  EXPECT_EQ(ApplyPuppetCommand(empty, puppet_edit::ScaleToSize{.width = 64, .height = 64}).code(),
            absl::StatusCode::kFailedPrecondition);

  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::ScaleToSize{.width = 0, .height = 64}).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(document.width, 64);
}

TEST(PuppetDocumentTest, RemovingAPartClearsItFromEveryFrameDrawOrder) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, RemovePart{.name = "arm"});

  for (const PuppetDocumentFrame& frame : document.frames) {
    EXPECT_EQ(frame.draw_order, (std::vector<std::string>{"body"}));
  }
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, RejectsAPartWhoseTwoBonesDoNotMeet) {
  PuppetDocument document = SkeletonDocument();
  ApplyOrDie(document, AddJoint{.name = "knee", .rest = {.x = 22, .y = 60}, .chain = "leg"});
  ApplyOrDie(document, AddBone{.name = "thigh", .start_joint = "hip", .end_joint = "knee"});

  const absl::Status applied =
      ApplyPuppetCommand(document, AddPart{.name = "wrong", .bones = {"forearm", "thigh"}});
  EXPECT_EQ(applied.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(document.parts.empty());
}

TEST(PuppetDocumentTest, RejectsAPartWithThreeBonesOrNone) {
  PuppetDocument document = SkeletonDocument();
  EXPECT_FALSE(ApplyPuppetCommand(
                   document, AddPart{.name = "three", .bones = {"upper_arm", "forearm", "torso"}})
                   .ok());
  EXPECT_FALSE(ApplyPuppetCommand(document, AddPart{.name = "none", .bones = {}}).ok());
}

TEST(PuppetDocumentTest, RejectsAPartNameThatCannotBecomeAFile) {
  PuppetDocument document = SkeletonDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, AddPart{.name = "../evil", .bones = {"torso"}}).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(PuppetDocumentTest, APartOnlyExcludesPartsDeclaredBeforeIt) {
  PuppetDocument document = BuildableDocument();
  // parts are declared arm, then body.
  ApplyOrDie(document, puppet_edit::SetPartExcludeParts{.part = "body", .excluded = {"arm"}});
  EXPECT_EQ(document.parts[1].exclude_parts, (std::vector<std::string>{"arm"}));

  EXPECT_EQ(ApplyPuppetCommand(
                document, puppet_edit::SetPartExcludeParts{.part = "arm", .excluded = {"body"}})
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ApplyPuppetCommand(document,
                               puppet_edit::SetPartExcludeParts{.part = "arm", .excluded = {"arm"}})
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ApplyPuppetCommand(
                document, puppet_edit::SetPartExcludeParts{.part = "body", .excluded = {"tail"}})
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(PuppetDocumentTest, CarvingHolesNeedsAnOutlineToCarveThemFrom) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, AddPart{.name = "bare", .bones = {"torso"}});
  const absl::Status applied = ApplyPuppetCommand(
      document, puppet_edit::SetPartExcludeOutlines{.part = "bare", .outlines = Square()});
  EXPECT_EQ(applied.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(document.parts.back().exclude_outlines.empty());
}

TEST(PuppetDocumentTest, FillsAreStoredWithTheirColour) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document,
             puppet_edit::SetPartFills{
                 .part = "arm", .fills = {{.polygon = Square().front(), .color = {9, 8, 7, 255}}}});
  ASSERT_EQ(document.parts.front().fills.size(), 1u);
  EXPECT_EQ(document.parts.front().fills.front().color[0], 9);
}

TEST(PuppetDocumentTest, RejectsAFillPolygonWithTooFewPoints) {
  PuppetDocument document = BuildableDocument();
  const absl::Status applied = ApplyPuppetCommand(
      document, puppet_edit::SetPartFills{
                    .part = "arm",
                    .fills = {{.polygon = LayeredPuppetPolygon{.points = {{.x = 0, .y = 0}}}}}});
  EXPECT_EQ(applied.code(), absl::StatusCode::kInvalidArgument);
}

TEST(PuppetDocumentTest, RemovingAnExcludedPartIsCaughtByValidation) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, puppet_edit::SetPartExcludeParts{.part = "body", .excluded = {"arm"}});
  EXPECT_FALSE(ApplyPuppetCommand(document, RemovePart{.name = "arm"}).ok());
  EXPECT_EQ(document.parts.size(), 2u);
}

TEST(PuppetDocumentTest, RejectsAnOutlineWithTooFewPoints) {
  PuppetDocument document = BuildableDocument();
  const absl::Status applied = ApplyPuppetCommand(
      document,
      SetPartOutline{
          .part = "arm",
          .outlines = {LayeredPuppetPolygon{.points = {{.x = 1, .y = 1}, {.x = 2, .y = 2}}}}});
  EXPECT_EQ(applied.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(document.parts.front().outlines.front().points.size(), 4u);
}

TEST(PuppetDocumentTest, RejectsMeshSpacingBelowOnePixel) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, SetPartMesh{.part = "arm", .spacing = 0}).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(PuppetDocumentTest, DrawOrderMustNameEveryPartExactlyOnce) {
  PuppetDocument document = BuildableDocument();
  EXPECT_FALSE(
      ApplyPuppetCommand(document, SetDrawOrder{.frame = "run_01", .order = {"body"}}).ok());
  EXPECT_FALSE(
      ApplyPuppetCommand(document, SetDrawOrder{.frame = "run_01", .order = {"body", "body"}})
          .ok());
  ApplyOrDie(document, SetDrawOrder{.frame = "run_01", .order = {"body", "arm"}});
  EXPECT_EQ(document.frames.front().draw_order, (std::vector<std::string>{"body", "arm"}));
}

TEST(PuppetDocumentTest, RemovingTheAnchorFrameMovesTheAnchor) {
  PuppetDocument document = BuildableDocument();
  ASSERT_EQ(document.anchor_frame, "run_01");
  ApplyOrDie(document, RemoveFrame{.name = "run_01"});
  EXPECT_EQ(document.anchor_frame, "run_02");

  ApplyOrDie(document, RemoveFrame{.name = "run_02"});
  ApplyOrDie(document, RemoveFrame{.name = "run_03"});
  EXPECT_TRUE(document.anchor_frame.empty());
}

TEST(PuppetDocumentTest, ReorderingFramesKeepsEveryPoseWithItsName) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, PoseJoint{.frame = "run_02", .joint = "elbow", .point = {.x = 55, .y = 30}});
  ApplyOrDie(document, puppet_edit::ReorderFrames{.order = {"run_03", "run_01", "run_02"}});

  EXPECT_EQ(document.frames[0].name, "run_03");
  EXPECT_EQ(document.frames[1].name, "run_01");
  EXPECT_EQ(document.frames[2].name, "run_02");
  EXPECT_EQ(document.frames[2].pose.at("elbow").x, 55);
  EXPECT_EQ(document.anchor_frame, "run_01");
}

TEST(PuppetDocumentTest, AReorderMustNameEveryFrameExactlyOnce) {
  PuppetDocument document = BuildableDocument();
  const std::vector<std::string> before = {document.frames[0].name, document.frames[1].name,
                                           document.frames[2].name};
  for (const std::vector<std::string>& order :
       {std::vector<std::string>{"run_01", "run_02"},
        std::vector<std::string>{"run_01", "run_02", "run_02"},
        std::vector<std::string>{"run_01", "run_02", "run_03", "run_04"},
        std::vector<std::string>{"run_01", "run_02", "ghost"}}) {
    EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::ReorderFrames{.order = order}).code(),
              absl::StatusCode::kInvalidArgument);
  }
  EXPECT_EQ(document.frames[0].name, before[0]);
  EXPECT_EQ(document.frames[2].name, before[2]);
}

TEST(PuppetDocumentTest, RenamingAFrameCarriesTheAnchorWithIt) {
  PuppetDocument document = BuildableDocument();
  ASSERT_EQ(document.anchor_frame, "run_01");
  ApplyOrDie(document, puppet_edit::RenameFrame{.name = "run_01", .new_name = "contact"});

  EXPECT_EQ(document.frames[0].name, "contact");
  EXPECT_EQ(document.anchor_frame, "contact");
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, RenamingLeavesOtherFramesAlone) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, puppet_edit::RenameFrame{.name = "run_02", .new_name = "passing"});
  EXPECT_EQ(document.frames[1].name, "passing");
  EXPECT_EQ(document.anchor_frame, "run_01");
}

TEST(PuppetDocumentTest, RenamingRejectsAnUnknownOrTakenOrEmptyName) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::RenameFrame{.name = "ghost", .new_name = "a"})
                .code(),
            absl::StatusCode::kNotFound);
  EXPECT_EQ(
      ApplyPuppetCommand(document, puppet_edit::RenameFrame{.name = "run_01", .new_name = "run_02"})
          .code(),
      absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::RenameFrame{.name = "run_01", .new_name = ""})
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(document.frames[0].name, "run_01");
}

TEST(PuppetDocumentTest, RenamingAFrameToItsOwnNameIsAllowed) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, puppet_edit::RenameFrame{.name = "run_01", .new_name = "run_01"});
  EXPECT_EQ(document.frames[0].name, "run_01");
}

TEST(PuppetDocumentTest, RebasingSlidesTheClipWithoutTouchingBoneLengths) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, MoveRestJoint{.name = "elbow", .rest = {.x = 130, .y = 90}});
  ApplyOrDie(document, PoseJoint{.frame = "run_02", .joint = "elbow", .point = {.x = 36, .y = 30}});

  const auto length = [](const PuppetPose& pose, const std::string& from, const std::string& to) {
    return std::hypot(pose.at(to).x - pose.at(from).x, pose.at(to).y - pose.at(from).y);
  };
  const double before_one = length(document.frames[0].pose, "shoulder", "elbow");
  const double before_two = length(document.frames[1].pose, "shoulder", "elbow");

  ApplyOrDie(document, puppet_edit::RebaseFrames{.from_frame = "run_01"});

  // Every joint moved by the same amount, so no bone changed length.
  EXPECT_NEAR(length(document.frames[0].pose, "shoulder", "elbow"), before_one, 1e-9);
  EXPECT_NEAR(length(document.frames[1].pose, "shoulder", "elbow"), before_two, 1e-9);
}

TEST(PuppetDocumentTest, RebasingMovesEveryFrameByTheSameAmount) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, MoveRestJoint{.name = "elbow", .rest = {.x = 130, .y = 90}});
  ApplyOrDie(document, PoseJoint{.frame = "run_02", .joint = "elbow", .point = {.x = 36, .y = 30}});

  const PuppetDocument before = document;
  ApplyOrDie(document, puppet_edit::RebaseFrames{.from_frame = "run_01"});

  const double slide_x = document.frames[0].pose.at("hip").x - before.frames[0].pose.at("hip").x;
  const double slide_y = document.frames[0].pose.at("hip").y - before.frames[0].pose.at("hip").y;
  for (size_t index = 0; index < document.frames.size(); ++index) {
    for (const auto& [name, point] : document.frames[index].pose) {
      EXPECT_NEAR(point.x - before.frames[index].pose.at(name).x, slide_x, 1e-9) << name;
      EXPECT_NEAR(point.y - before.frames[index].pose.at(name).y, slide_y, 1e-9) << name;
    }
  }
}

TEST(PuppetDocumentTest, RebasingBringsTheClipCentreOntoTheRestPoseCentre) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, MoveRestJoint{.name = "elbow", .rest = {.x = 130, .y = 90}});
  ApplyOrDie(document, puppet_edit::RebaseFrames{.from_frame = "run_01"});

  double rest_x = 0;
  double frame_x = 0;
  for (const auto& [name, rest] : document.rest_pose) {
    rest_x += rest.x;
    frame_x += document.frames[0].pose.at(name).x;
  }
  EXPECT_NEAR(rest_x, frame_x, 1e-9);
}

TEST(PuppetDocumentTest, RebasingLeavesTheRestPoseAndFrameNamesAlone) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, MoveRestJoint{.name = "wrist", .rest = {.x = 200, .y = 200}});
  ApplyOrDie(document, puppet_edit::RebaseFrames{.from_frame = "run_03"});

  EXPECT_EQ(document.rest_pose.at("wrist").x, 200);
  EXPECT_EQ(document.frames.size(), 3u);
  EXPECT_EQ(document.frames[0].name, "run_01");
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, RebasingRejectsAnUnknownFrame) {
  PuppetDocument document = BuildableDocument();
  const absl::Status applied =
      ApplyPuppetCommand(document, puppet_edit::RebaseFrames{.from_frame = "ghost"});
  EXPECT_EQ(applied.code(), absl::StatusCode::kNotFound);
}

TEST(PuppetDocumentTest, FittingBoneLengthsKeepsDirectionAndDropsScaling) {
  PuppetDocument document = BuildableDocument();
  // shoulder-elbow is 10*sqrt(2) at rest; elbow-wrist likewise. Squash the arm
  // in one frame the way a flat clip foreshortens it.
  ApplyOrDie(document, PoseJoint{.frame = "run_01", .joint = "elbow", .point = {.x = 25, .y = 25}});
  ApplyOrDie(document, PoseJoint{.frame = "run_01", .joint = "wrist", .point = {.x = 28, .y = 28}});

  ApplyOrDie(document, puppet_edit::FitBoneLengths{.frame = "run_01"});

  const PuppetPose& fitted = document.frames[0].pose;
  const auto length = [](const ProfileControlPoint& from, const ProfileControlPoint& to) {
    return std::hypot(to.x - from.x, to.y - from.y);
  };
  const double rest_upper =
      length(document.rest_pose.at("shoulder"), document.rest_pose.at("elbow"));
  const double rest_lower = length(document.rest_pose.at("elbow"), document.rest_pose.at("wrist"));
  EXPECT_NEAR(length(fitted.at("shoulder"), fitted.at("elbow")), rest_upper, 1e-9);
  EXPECT_NEAR(length(fitted.at("elbow"), fitted.at("wrist")), rest_lower, 1e-9);
  // The arm still points down and to the right, the way the squashed frame did.
  EXPECT_GT(fitted.at("elbow").x, fitted.at("shoulder").x);
  EXPECT_GT(fitted.at("wrist").x, fitted.at("elbow").x);
}

TEST(PuppetDocumentTest, FittingBoneLengthsLeavesARootJointWhereItIs) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document,
             PoseJoint{.frame = "run_01", .joint = "shoulder", .point = {.x = 44, .y = 12}});
  ApplyOrDie(document, puppet_edit::FitBoneLengths{.frame = "run_01"});

  // shoulder starts every bone and ends none, so nothing corrects it.
  EXPECT_EQ(document.frames[0].pose.at("shoulder").x, 44);
  EXPECT_EQ(document.frames[0].pose.at("shoulder").y, 12);
}

TEST(PuppetDocumentTest, FittingBoneLengthsLeavesOtherFramesAndTheRestPoseAlone) {
  PuppetDocument document = BuildableDocument();
  ApplyOrDie(document, PoseJoint{.frame = "run_02", .joint = "elbow", .point = {.x = 99, .y = 99}});
  ApplyOrDie(document, puppet_edit::FitBoneLengths{.frame = "run_01"});

  EXPECT_EQ(document.frames[1].pose.at("elbow").x, 99);
  EXPECT_EQ(document.rest_pose.at("elbow").x, 30);
}

TEST(PuppetDocumentTest, ACollapsedBoneFallsBackToTheRestDirection) {
  PuppetDocument document = BuildableDocument();
  // The frame squashes shoulder-elbow to nothing, so there is no direction to
  // keep and the rest direction is used instead.
  ApplyOrDie(document, PoseJoint{.frame = "run_01", .joint = "elbow", .point = {.x = 20, .y = 20}});
  ApplyOrDie(document, puppet_edit::FitBoneLengths{.frame = "run_01"});

  const PuppetPose& fitted = document.frames[0].pose;
  EXPECT_EQ(fitted.at("elbow").x, 30);
  EXPECT_EQ(fitted.at("elbow").y, 30);
  // Every bone still comes out at its rest length, collapsed one included.
  const auto length = [](const ProfileControlPoint& from, const ProfileControlPoint& to) {
    return std::hypot(to.x - from.x, to.y - from.y);
  };
  EXPECT_NEAR(length(fitted.at("shoulder"), fitted.at("elbow")),
              length(document.rest_pose.at("shoulder"), document.rest_pose.at("elbow")), 1e-9);
  EXPECT_NEAR(length(fitted.at("elbow"), fitted.at("wrist")),
              length(document.rest_pose.at("elbow"), document.rest_pose.at("wrist")), 1e-9);
}

TEST(PuppetDocumentTest, FittingBoneLengthsRejectsAnUnknownFrame) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, puppet_edit::FitBoneLengths{.frame = "ghost"}).code(),
            absl::StatusCode::kNotFound);
}

TEST(PuppetDocumentTest, AnchorMustNameAnExistingFrame) {
  PuppetDocument document = BuildableDocument();
  EXPECT_EQ(ApplyPuppetCommand(document, SetAnchorFrame{.name = "nope"}).code(),
            absl::StatusCode::kNotFound);
  ApplyOrDie(document, SetAnchorFrame{.name = "run_03"});
  EXPECT_EQ(document.anchor_frame, "run_03");
}

TEST(PuppetDocumentTest, ImportSkeletonTakesJointsBonesAndFrames) {
  PuppetDocument document;
  ApplyOrDie(document, SetSourceImage{.path = "mouse.png", .width = 64, .height = 64});
  ApplyOrDie(document, ImportSkeleton{.rig = TwoFrameRig(),
                                      .rig_path = "rig-bench.json",
                                      .clip_id = "run",
                                      .import_frames = true});

  EXPECT_EQ(document.rest_pose.size(), 3u);
  EXPECT_EQ(document.bones.size(), 2u);
  EXPECT_EQ(document.bones.front().name, "hip-knee");
  ASSERT_EQ(document.frames.size(), 2u);
  EXPECT_EQ(document.frames[0].name, "contact");
  EXPECT_EQ(document.frames[1].pose.at("knee").x, 18);
  EXPECT_EQ(document.anchor_frame, "contact");
  EXPECT_EQ(document.skeleton_source.clip_id, "run");
  // The rest pose is the clip's first frame, since a Rig Bench file has none.
  EXPECT_EQ(document.rest_pose.at("knee").x, 12);
}

TEST(PuppetDocumentTest, ImportSkeletonWithoutFramesKeepsFrameNamesAndDrawOrders) {
  PuppetDocument document;
  ApplyOrDie(document, SetSourceImage{.path = "mouse.png", .width = 64, .height = 64});
  ApplyOrDie(document, AddJoint{.name = "old", .rest = {.x = 1, .y = 1}, .chain = "spine"});
  ApplyOrDie(document, AddFrames{.name_prefix = "keep", .count = 2});
  ApplyOrDie(document, ImportSkeleton{.rig = TwoFrameRig(),
                                      .rig_path = "rig-bench.json",
                                      .clip_id = "run",
                                      .import_frames = false});

  ASSERT_EQ(document.frames.size(), 2u);
  EXPECT_EQ(document.frames[0].name, "keep_01");
  EXPECT_FALSE(document.frames[0].pose.contains("old"));
  EXPECT_TRUE(document.frames[0].pose.contains("ankle"));
  EXPECT_OK(ValidatePuppetDocument(document));
}

TEST(PuppetDocumentTest, ImportSkeletonIsRefusedOncePartsExist) {
  PuppetDocument document = BuildableDocument();
  const absl::Status applied = ApplyPuppetCommand(
      document, ImportSkeleton{.rig = TwoFrameRig(), .rig_path = "rig.json", .clip_id = "run"});
  EXPECT_EQ(applied.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(document.bones.size(), 3u);
}

TEST(PuppetDocumentTest, ImportSkeletonRejectsAnUnknownClip) {
  PuppetDocument document;
  const absl::Status applied = ApplyPuppetCommand(
      document, ImportSkeleton{.rig = TwoFrameRig(), .rig_path = "rig.json", .clip_id = "walk"});
  EXPECT_FALSE(applied.ok());
  EXPECT_TRUE(document.rest_pose.empty());
}

TEST(PuppetDocumentTest, ARejectedCommandLeavesTheDocumentUntouched) {
  const PuppetDocument original = BuildableDocument();
  PuppetDocument document = original;
  EXPECT_FALSE(ApplyPuppetCommand(document, AddPart{.name = "arm", .bones = {"torso"}}).ok());

  EXPECT_EQ(document.parts.size(), original.parts.size());
  EXPECT_EQ(document.frames.size(), original.frames.size());
  for (size_t index = 0; index < document.frames.size(); ++index) {
    EXPECT_EQ(document.frames[index].draw_order, original.frames[index].draw_order);
  }
}

TEST(PuppetDocumentTest, ValidateCatchesAFrameThatDoesNotPoseEveryJoint) {
  PuppetDocument document = BuildableDocument();
  document.frames.front().pose.erase("elbow");
  const absl::Status status = ValidatePuppetDocument(document);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("run_01"), std::string::npos);
}

TEST(PuppetDocumentTest, ValidateCatchesABoneOnAMissingJoint) {
  PuppetDocument document = BuildableDocument();
  document.rest_pose.erase("wrist");
  EXPECT_FALSE(ValidatePuppetDocument(document).ok());
}

TEST(PuppetDocumentTest, ValidateCatchesADrawOrderMissingAPart) {
  PuppetDocument document = BuildableDocument();
  document.frames.front().draw_order.pop_back();
  EXPECT_FALSE(ValidatePuppetDocument(document).ok());
}

TEST(PuppetDocumentTest, ValidateCatchesAnAnchorThatIsNotAFrame) {
  PuppetDocument document = BuildableDocument();
  document.anchor_frame = "ghost";
  EXPECT_FALSE(ValidatePuppetDocument(document).ok());
}

}  // namespace
}  // namespace zebes
