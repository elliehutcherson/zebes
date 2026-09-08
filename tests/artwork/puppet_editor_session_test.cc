#include "artwork/puppet_editor_session.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "artwork/layered_puppet.h"
#include "artwork/puppet_document.h"
#include "artwork/puppet_document_json.h"
#include "artwork/skeleton_rig.h"
#include "common/image_io.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

using puppet_edit::AddFrames;
using puppet_edit::AddPart;
using puppet_edit::Command;
using puppet_edit::PoseJoint;
using puppet_edit::SetPartOutline;

// A test tree holding one source PNG and one document, torn down with the
// fixture so a failed run leaves nothing behind.
class PuppetEditorSessionTest : public testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("zebes-puppet-session-" +
             std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
    WriteSource();
    document_path_ = root_ / "puppet.json";
    ASSERT_OK(SavePuppetDocument(document_path_, BuildableDocument()));
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  void WriteSource() {
    constexpr int kSize = 64;
    std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4, 0);
    for (int y = 8; y < 56; ++y) {
      for (int x = 8; x < 56; ++x) {
        const size_t offset = (static_cast<size_t>(y) * kSize + x) * 4;
        pixels[offset] = 200;
        pixels[offset + 1] = 120;
        pixels[offset + 2] = 80;
        pixels[offset + 3] = 255;
      }
    }
    ASSERT_OK(WritePng((root_ / "mouse.png").string(), kSize, kSize, pixels));
  }

  static std::vector<LayeredPuppetPolygon> Square(double left, double top) {
    return {LayeredPuppetPolygon{.points = {{.x = left, .y = top},
                                            {.x = left + 20, .y = top},
                                            {.x = left + 20, .y = top + 20},
                                            {.x = left, .y = top + 20}}}};
  }

  static PuppetDocument BuildableDocument() {
    PuppetDocument document;
    document.source_image = "mouse.png";
    document.width = 64;
    document.height = 64;
    document.rest_pose = {{"shoulder", {.x = 20, .y = 20}},
                          {"elbow", {.x = 30, .y = 30}},
                          {"wrist", {.x = 40, .y = 40}},
                          {"hip", {.x = 20, .y = 50}}};
    document.joint_chains = {
        {"shoulder", "spine"}, {"elbow", "arm"}, {"wrist", "arm"}, {"hip", "spine"}};
    document.bones = {{.name = "upper_arm", .start_joint = "shoulder", .end_joint = "elbow"},
                      {.name = "forearm", .start_joint = "elbow", .end_joint = "wrist"},
                      {.name = "torso", .start_joint = "shoulder", .end_joint = "hip"}};
    document.parts = {
        {.name = "arm", .bones = {"upper_arm", "forearm"}, .outlines = Square(20, 20)},
        {.name = "body", .bones = {"torso"}, .outlines = Square(12, 12)}};
    document.frames = {{.name = "f1", .pose = document.rest_pose, .draw_order = {"body", "arm"}},
                       {.name = "f2", .pose = document.rest_pose, .draw_order = {"body", "arm"}}};
    document.anchor_frame = "f1";
    return document;
  }

  std::unique_ptr<PuppetEditorSession> Open() {
    absl::StatusOr<std::unique_ptr<PuppetEditorSession>> session = PuppetEditorSession::Create(
        {.document_path = document_path_, .asset_root = root_, .rig_root = root_});
    EXPECT_OK(session);
    return session.ok() ? *std::move(session) : nullptr;
  }

  std::filesystem::path root_;
  std::filesystem::path document_path_;
};

TEST_F(PuppetEditorSessionTest, BuildsArtworkOnOpen) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  ASSERT_NE(session->built(), nullptr);
  EXPECT_TRUE(session->build_blocker().empty());
  EXPECT_EQ(session->built()->parts.size(), 2u);
  EXPECT_EQ(session->built()->poses.size(), 2u);
}

TEST_F(PuppetEditorSessionTest, AnUnfinishedDocumentBuildsNothingAndSaysWhy) {
  PuppetDocument document = BuildableDocument();
  document.parts.front().outlines.clear();
  ASSERT_OK(SavePuppetDocument(document_path_, document));

  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->built(), nullptr);
  EXPECT_EQ(session->build_blocker(), "outline or fill part 'arm'");
}

TEST_F(PuppetEditorSessionTest, AcceptedCommandsAreSavedToDisk) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  const std::vector<Command> commands = {AddFrames{.name_prefix = "run", .count = 3}};
  ASSERT_OK(session->ApplyCommands(commands));

  EXPECT_EQ(session->document().frames.size(), 5u);
  const absl::StatusOr<PuppetDocument> reloaded = LoadPuppetDocument(document_path_);
  ASSERT_OK(reloaded);
  EXPECT_EQ(reloaded->frames.size(), 5u);
}

TEST_F(PuppetEditorSessionTest, ARejectedBatchNamesTheCommandAndChangesNothing) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  const std::vector<Command> commands = {AddFrames{.name_prefix = "run", .count = 1},
                                         AddPart{.name = "arm", .bones = {"torso"}}};
  const absl::Status applied = session->ApplyCommands(commands);
  ASSERT_FALSE(applied.ok());
  EXPECT_NE(applied.message().find("command[1]"), std::string::npos);

  // The first command in the batch must not survive the second one failing.
  EXPECT_EQ(session->document().frames.size(), 2u);
  const absl::StatusOr<PuppetDocument> reloaded = LoadPuppetDocument(document_path_);
  ASSERT_OK(reloaded);
  EXPECT_EQ(reloaded->frames.size(), 2u);
}

TEST_F(PuppetEditorSessionTest, PosingAJointKeepsTheArtworkItDidNotChange) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  const std::vector<uint8_t> before = *session->EncodedPart("body");

  ASSERT_TRUE(
      session
          ->ApplyCommands({PoseJoint{.frame = "f2", .joint = "elbow", .point = {.x = 44, .y = 30}}})
          .ok());

  ASSERT_NE(session->built(), nullptr);
  // Posing changes where a part is drawn, never which pixels it owns.
  EXPECT_EQ(*session->EncodedPart("body"), before);
  EXPECT_EQ(session->built()->poses[1].joints.size(), 4u);
}

TEST_F(PuppetEditorSessionTest, ChangingAnOutlineChangesThatPartsArtwork) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  const std::vector<uint8_t> before = *session->EncodedPart("body");

  ASSERT_TRUE(
      session->ApplyCommands({SetPartOutline{.part = "body", .outlines = Square(30, 30)}}).ok());
  EXPECT_NE(*session->EncodedPart("body"), before);
}

TEST_F(PuppetEditorSessionTest, PartPngIsServedForEveryBuiltPart) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  for (const LayeredPuppetPart& part : session->built()->parts) {
    const absl::StatusOr<std::vector<uint8_t>> encoded = session->EncodedPart(part.name);
    ASSERT_OK(encoded);
    EXPECT_FALSE(encoded->empty());
  }
  EXPECT_EQ(session->EncodedPart("tail").status().code(), absl::StatusCode::kNotFound);
}

TEST_F(PuppetEditorSessionTest, SourceImageIsServedAndAMissingGuideIsNotFound) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  const absl::StatusOr<std::vector<uint8_t>> source = session->EncodedSourceImage();
  ASSERT_OK(source);
  EXPECT_FALSE(source->empty());
  EXPECT_EQ(session->EncodedGuideImage().status().code(), absl::StatusCode::kNotFound);
}

TEST_F(PuppetEditorSessionTest, ListsThePngFilesWithTheSizeEachOneReallyIs) {
  std::filesystem::create_directories(root_ / "guides");
  const std::vector<uint8_t> pixels(static_cast<size_t>(6) * 3 * 4, 0);
  ASSERT_OK(WritePng((root_ / "guides" / "biped.png").string(), 6, 3, pixels));
  std::ofstream(root_ / "notes.txt") << "not artwork";
  std::ofstream(root_ / "broken.png") << "not a png";

  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  const std::vector<PuppetEditorSession::AvailableImage> images = session->AvailableImages();
  ASSERT_EQ(images.size(), 2u);
  EXPECT_EQ(images[0].path, "guides/biped.png");
  EXPECT_EQ(images[0].width, 6);
  EXPECT_EQ(images[0].height, 3);
  EXPECT_EQ(images[1].path, "mouse.png");
  EXPECT_EQ(images[1].width, 64);
  EXPECT_EQ(images[1].height, 64);
}

TEST_F(PuppetEditorSessionTest, ListsRigsWithTheirClipsAndSkipsUnreadableOnes) {
  const std::filesystem::path rigs = root_ / "rigs";
  std::filesystem::create_directories(rigs);
  std::ofstream(rigs / "good.json") << R"({
    "version": 2, "floor_y": 20, "updated_at": "2026-09-07T00:00:00Z",
    "points": [{"name": "hip", "chain": "spine"}, {"name": "knee", "chain": "leg_l"}],
    "bones": [{"start": "hip", "end": "knee"}],
    "clips": {"run": {"fps": 8, "name": "run", "frames": [
      {"label": "a", "underlay": "", "pose": {"hip": [1, 2], "knee": [3, 4]}}]}}
  })";
  std::ofstream(rigs / "broken.json") << "{ not a rig";

  const absl::StatusOr<std::unique_ptr<PuppetEditorSession>> session = PuppetEditorSession::Create(
      {.document_path = document_path_, .asset_root = root_, .rig_root = rigs});
  ASSERT_OK(session);

  const std::vector<PuppetEditorSession::AvailableRig> listed = (*session)->AvailableRigs();
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed.front().path, "good.json");
  EXPECT_EQ(listed.front().clips, (std::vector<std::string>{"run"}));
}

TEST_F(PuppetEditorSessionTest, SavesTheSkeletonAsARigAndReplacesOneOfTheSameName) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);

  ASSERT_OK(session->SaveRig("authored-rig", "walk"));
  const std::filesystem::path written = root_ / "authored-rig.json";
  ASSERT_TRUE(std::filesystem::exists(written));

  const absl::StatusOr<SkeletonRig> rig = LoadSkeletonRig(written);
  ASSERT_OK(rig);
  EXPECT_EQ(rig->points.size(), 4u);
  EXPECT_EQ(rig->bones.size(), 3u);
  ASSERT_EQ(rig->clips.size(), 1u);
  EXPECT_EQ(rig->clips.front().id, "walk");

  // Replaced rather than refused: importing copies a skeleton into the document
  // that imports it, so rewriting the file changes no existing puppet.
  ASSERT_TRUE(session
                  ->ApplyCommands({puppet_edit::AddJoint{
                      .name = "tail", .rest = {.x = 8, .y = 52}, .chain = "tail"}})
                  .ok());
  ASSERT_OK(session->SaveRig("authored-rig", "walk"));
  const absl::StatusOr<SkeletonRig> replaced = LoadSkeletonRig(written);
  ASSERT_OK(replaced);
  EXPECT_EQ(replaced->points.size(), 5u);

  EXPECT_EQ(session->SaveRig("../escape", "walk").code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(PuppetEditorSessionTest, OverlappingPartsAreReportedWithThePixelsTheyShare) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  // The fixture's two squares cross at x 20..32, y 20..32, and the build does
  // not resolve that: both parts own those pixels and both draw them. This is
  // the state the report exists to make visible.
  const std::vector<PuppetEditorSession::SharedPixels> shared = session->OverlappingParts();
  ASSERT_EQ(shared.size(), 1u);
  EXPECT_EQ(shared.front().first, "arm");
  EXPECT_EQ(shared.front().second, "body");
  EXPECT_GT(shared.front().pixels, 0u);
}

TEST_F(PuppetEditorSessionTest, ExcludingThePartInFrontClearsTheOverlap) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  ASSERT_FALSE(session->OverlappingParts().empty());

  ASSERT_TRUE(
      session
          ->ApplyCommands({puppet_edit::SetPartExcludeParts{.part = "body", .excluded = {"arm"}}})
          .ok());
  EXPECT_TRUE(session->OverlappingParts().empty());
}

TEST_F(PuppetEditorSessionTest, RefusesADocumentThatDoesNotExist) {
  const absl::StatusOr<std::unique_ptr<PuppetEditorSession>> session =
      PuppetEditorSession::Create({.document_path = root_ / "absent.json", .asset_root = root_});
  EXPECT_FALSE(session.ok());
}

TEST_F(PuppetEditorSessionTest, ADocumentWhoseSourceImageIsMissingStillOpens) {
  PuppetDocument document = BuildableDocument();
  document.source_image = "absent.png";
  ASSERT_OK(SavePuppetDocument(document_path_, document));

  const absl::StatusOr<std::unique_ptr<PuppetEditorSession>> session =
      PuppetEditorSession::Create({.document_path = document_path_, .asset_root = root_});
  ASSERT_OK(session);
  EXPECT_EQ((*session)->built(), nullptr);
  EXPECT_NE((*session)->build_blocker().find("absent.png"), std::string::npos)
      << (*session)->build_blocker();
}

TEST_F(PuppetEditorSessionTest, ADocumentSizedUnlikeItsPictureOpensAndSaysBothSizes) {
  PuppetDocument document = BuildableDocument();
  document.width = 128;
  document.height = 128;
  ASSERT_OK(SavePuppetDocument(document_path_, document));

  const absl::StatusOr<std::unique_ptr<PuppetEditorSession>> session =
      PuppetEditorSession::Create({.document_path = document_path_, .asset_root = root_});
  ASSERT_OK(session);
  EXPECT_EQ((*session)->built(), nullptr);
  EXPECT_NE((*session)->build_blocker().find("128 x 128"), std::string::npos)
      << (*session)->build_blocker();
  EXPECT_NE((*session)->build_blocker().find("64 x 64"), std::string::npos)
      << (*session)->build_blocker();
}

TEST_F(PuppetEditorSessionTest, StretchingAMissizedDocumentOntoItsPictureBuildsIt) {
  PuppetDocument document = BuildableDocument();
  document.width = 128;
  document.height = 128;
  ASSERT_OK(SavePuppetDocument(document_path_, document));
  const absl::StatusOr<std::unique_ptr<PuppetEditorSession>> session =
      PuppetEditorSession::Create({.document_path = document_path_, .asset_root = root_});
  ASSERT_OK(session);

  ASSERT_TRUE(
      (*session)->ApplyCommands({puppet_edit::ScaleToSize{.width = 64, .height = 64}}).ok());
  EXPECT_NE((*session)->built(), nullptr);
  EXPECT_TRUE((*session)->build_blocker().empty());
}

TEST_F(PuppetEditorSessionTest, ACommandThatCannotBuildRollsBackAndKeepsTheOldArtwork) {
  const std::unique_ptr<PuppetEditorSession> session = Open();
  ASSERT_NE(session, nullptr);
  const std::vector<uint8_t> before = *session->EncodedPart("body");

  // Pointing at artwork that is not there passes document validation and fails
  // the rebuild, which is the path where the rollback has to do real work.
  const absl::Status applied = session->ApplyCommands(
      {puppet_edit::SetSourceImage{.path = "absent.png", .width = 64, .height = 64}});
  ASSERT_FALSE(applied.ok());

  EXPECT_EQ(session->document().source_image, "mouse.png");
  ASSERT_NE(session->built(), nullptr);
  EXPECT_EQ(*session->EncodedPart("body"), before);

  const absl::StatusOr<PuppetDocument> reloaded = LoadPuppetDocument(document_path_);
  ASSERT_OK(reloaded);
  EXPECT_EQ(reloaded->source_image, "mouse.png");
}

}  // namespace
}  // namespace zebes
