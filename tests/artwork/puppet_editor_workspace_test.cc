#include "artwork/puppet_editor_workspace.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "artwork/puppet_document.h"
#include "artwork/puppet_document_json.h"
#include "artwork/skeleton_rig.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

class PuppetEditorWorkspaceTest : public testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("zebes-puppet-workspace-" +
             std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
    std::filesystem::remove_all(root_);
    documents_ = root_ / "documents";
    std::filesystem::create_directories(documents_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  std::unique_ptr<PuppetEditorWorkspace> Open() {
    absl::StatusOr<std::unique_ptr<PuppetEditorWorkspace>> workspace =
        PuppetEditorWorkspace::Create(
            {.document_root = documents_, .asset_root = root_, .rig_root = root_});
    EXPECT_OK(workspace);
    return workspace.ok() ? *std::move(workspace) : nullptr;
  }

  void WriteDocument(const std::string& name) {
    ASSERT_OK(SavePuppetDocument(documents_ / name, PuppetDocument{}));
  }

  std::filesystem::path root_;
  std::filesystem::path documents_;
};

TEST_F(PuppetEditorWorkspaceTest, StartsWithNothingOpen) {
  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  EXPECT_EQ(workspace->session(), nullptr);
  EXPECT_TRUE(workspace->open_document().empty());
  EXPECT_TRUE(workspace->AvailableDocuments().empty());
}

TEST_F(PuppetEditorWorkspaceTest, ListsOnlyDocumentNamesItWouldAccept) {
  WriteDocument("mouse.json");
  WriteDocument("badger-v2.json");
  std::ofstream(documents_ / "notes.txt") << "not a document";
  std::ofstream(documents_ / "has space.json") << "{}";

  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  EXPECT_EQ(workspace->AvailableDocuments(),
            (std::vector<std::string>{"badger-v2.json", "mouse.json"}));
}

TEST_F(PuppetEditorWorkspaceTest, OpensAndSwitchesBetweenDocuments) {
  WriteDocument("one.json");
  PuppetDocument named;
  named.source_image = "mouse.png";
  named.width = 8;
  named.height = 8;
  ASSERT_OK(SavePuppetDocument(documents_ / "two.json", named));

  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  ASSERT_OK(workspace->Open("one.json"));
  EXPECT_EQ(workspace->open_document(), "one.json");
  EXPECT_TRUE(workspace->session()->document().source_image.empty());

  ASSERT_OK(workspace->Open("two.json"));
  EXPECT_EQ(workspace->open_document(), "two.json");
  EXPECT_EQ(workspace->session()->document().source_image, "mouse.png");
}

TEST_F(PuppetEditorWorkspaceTest, AFailedOpenKeepsTheDocumentThatWasAlreadyOpen) {
  WriteDocument("good.json");
  std::ofstream(documents_ / "broken.json") << "{ not json";

  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  ASSERT_OK(workspace->Open("good.json"));
  EXPECT_FALSE(workspace->Open("broken.json").ok());
  EXPECT_EQ(workspace->open_document(), "good.json");
  EXPECT_NE(workspace->session(), nullptr);
}

TEST_F(PuppetEditorWorkspaceTest, CreatesAnEmptyDocumentAndOpensIt) {
  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  ASSERT_OK(workspace->CreateDocument("badger.json"));

  EXPECT_EQ(workspace->open_document(), "badger.json");
  ASSERT_NE(workspace->session(), nullptr);
  EXPECT_EQ(workspace->session()->built(), nullptr);
  EXPECT_EQ(workspace->session()->build_blocker(), "choose a source image");
  EXPECT_EQ(workspace->AvailableDocuments(), (std::vector<std::string>{"badger.json"}));
  EXPECT_TRUE(std::filesystem::exists(documents_ / "badger.json"));
}

TEST_F(PuppetEditorWorkspaceTest, RefusesToOverwriteAnExistingDocument) {
  WriteDocument("mouse.json");
  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  EXPECT_EQ(workspace->CreateDocument("mouse.json").code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(PuppetEditorWorkspaceTest, RefusesANameThatCouldLeaveTheDirectory) {
  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  for (const std::string& name :
       {"../escape.json", "sub/mouse.json", "mouse.txt", ".json", "", "mouse"}) {
    EXPECT_EQ(workspace->Open(name).code(), absl::StatusCode::kInvalidArgument) << name;
    EXPECT_EQ(workspace->CreateDocument(name).code(), absl::StatusCode::kInvalidArgument) << name;
  }
  EXPECT_TRUE(workspace->AvailableDocuments().empty());
}

TEST_F(PuppetEditorWorkspaceTest, MakesTheDocumentDirectoryWhenItIsMissing) {
  const std::filesystem::path fresh = root_ / "brand-new";
  const absl::StatusOr<std::unique_ptr<PuppetEditorWorkspace>> workspace =
      PuppetEditorWorkspace::Create({.document_root = fresh, .asset_root = root_});
  ASSERT_OK(workspace);
  EXPECT_TRUE(std::filesystem::is_directory(fresh));
}

TEST_F(PuppetEditorWorkspaceTest, RefusesAWorkspaceWithNoDirectory) {
  EXPECT_FALSE(PuppetEditorWorkspace::Create({.asset_root = root_}).ok());
}

// A skeleton with two limbs, enough to grow and to hand to another document.
PuppetDocument RiggedDocument() {
  PuppetDocument document;
  const std::vector<puppet_edit::Command> commands = {
      puppet_edit::AddJoint{.name = "hip", .rest = {.x = 10, .y = 10}, .chain = "spine"},
      puppet_edit::AddJoint{.name = "knee", .rest = {.x = 10, .y = 20}, .chain = "leg"},
      puppet_edit::AddBone{.name = "thigh", .start_joint = "hip", .end_joint = "knee"},
  };
  for (const puppet_edit::Command& command : commands) {
    EXPECT_OK(ApplyPuppetCommand(document, command));
  }
  return document;
}

TEST_F(PuppetEditorWorkspaceTest, SavingARigTwiceReplacesIt) {
  ASSERT_OK(SavePuppetDocument(documents_ / "author.json", RiggedDocument()));
  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  ASSERT_OK(workspace->Open("author.json"));

  ASSERT_OK(workspace->SaveRig("biped", "authored", false));
  ASSERT_TRUE(workspace->session()
                  ->ApplyCommands({puppet_edit::AddJoint{
                      .name = "ankle", .rest = {.x = 10, .y = 30}, .chain = "leg"}})
                  .ok());
  ASSERT_OK(workspace->SaveRig("biped", "authored", false));

  const absl::StatusOr<SkeletonRig> rig = LoadSkeletonRig(root_ / "biped.json");
  ASSERT_OK(rig);
  EXPECT_EQ(rig->points.size(), 3u);
}

TEST_F(PuppetEditorWorkspaceTest, UpdatingARigAddsWhatItGainedToEveryDocumentUsingIt) {
  ASSERT_OK(SavePuppetDocument(documents_ / "author.json", RiggedDocument()));
  PuppetDocument user = RiggedDocument();
  user.skeleton_source = {.rig_path = "biped.json", .clip_id = "authored"};
  ASSERT_OK(SavePuppetDocument(documents_ / "user.json", user));
  // A document on a different rig must not be touched.
  PuppetDocument stranger = RiggedDocument();
  stranger.skeleton_source = {.rig_path = "other.json", .clip_id = "authored"};
  ASSERT_OK(SavePuppetDocument(documents_ / "stranger.json", stranger));

  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  ASSERT_OK(workspace->Open("author.json"));
  ASSERT_TRUE(
      workspace->session()
          ->ApplyCommands(
              {puppet_edit::AddJoint{.name = "ankle", .rest = {.x = 10, .y = 30}, .chain = "leg"},
               puppet_edit::AddBone{.name = "shin", .start_joint = "knee", .end_joint = "ankle"},
               puppet_edit::SetJointChain{.name = "hip", .chain = "pelvis"}})
          .ok());

  const absl::StatusOr<PuppetEditorWorkspace::RigUpdate> update =
      workspace->SaveRig("biped", "authored", true);
  ASSERT_OK(update);
  EXPECT_EQ(update->changed, (std::vector<std::string>{"user.json"}));
  EXPECT_TRUE(update->blocked.empty());

  const absl::StatusOr<PuppetDocument> updated = LoadPuppetDocument(documents_ / "user.json");
  ASSERT_OK(updated);
  EXPECT_TRUE(updated->rest_pose.contains("ankle"));
  EXPECT_EQ(updated->joint_chains.at("hip"), "pelvis");
  // Matched on the joints, not the name: the rig calls this bone "thigh" and
  // the imported document calls it "hip-knee", and only "shin" is new.
  EXPECT_EQ(updated->bones.size(), 2u);
  // The joints it already had keep the positions they were dragged to.
  EXPECT_DOUBLE_EQ(updated->rest_pose.at("knee").y, 20);

  const absl::StatusOr<PuppetDocument> untouched = LoadPuppetDocument(documents_ / "stranger.json");
  ASSERT_OK(untouched);
  EXPECT_FALSE(untouched->rest_pose.contains("ankle"));
}

TEST_F(PuppetEditorWorkspaceTest, ADocumentUsingAJointTheRigDroppedIsLeftAlone) {
  PuppetDocument author = RiggedDocument();
  ASSERT_OK(SavePuppetDocument(documents_ / "author.json", author));
  PuppetDocument user = RiggedDocument();
  user.skeleton_source = {.rig_path = "biped.json", .clip_id = "authored"};
  ASSERT_OK(SavePuppetDocument(documents_ / "user.json", user));

  const std::unique_ptr<PuppetEditorWorkspace> workspace = Open();
  ASSERT_NE(workspace, nullptr);
  ASSERT_OK(workspace->Open("author.json"));
  ASSERT_TRUE(
      workspace->session()
          ->ApplyCommands(
              {puppet_edit::RemoveBone{.name = "thigh"}, puppet_edit::RemoveJoint{.name = "knee"},
               puppet_edit::AddJoint{.name = "tail", .rest = {.x = 2, .y = 10}, .chain = "tail"},
               puppet_edit::AddBone{
                   .name = "tail_bone", .start_joint = "hip", .end_joint = "tail"}})
          .ok());

  const absl::StatusOr<PuppetEditorWorkspace::RigUpdate> update =
      workspace->SaveRig("biped", "authored", true);
  ASSERT_OK(update);
  EXPECT_EQ(update->blocked, (std::vector<std::string>{"user.json"}));
  EXPECT_TRUE(update->changed.empty());

  const absl::StatusOr<PuppetDocument> untouched = LoadPuppetDocument(documents_ / "user.json");
  ASSERT_OK(untouched);
  EXPECT_TRUE(untouched->rest_pose.contains("knee"));
  EXPECT_FALSE(untouched->rest_pose.contains("tail"));
}

}  // namespace
}  // namespace zebes
