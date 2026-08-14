#include "editor/blueprint_editor/collider_panel_model.h"

#include <cstdint>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(ColliderPanelModelTest, OrderedCatalogPreservesDuplicateNames) {
  ColliderPanelModel model;
  model.SetColliders({{.id = "b", .name = "Body"}, {.id = "a", .name = "Body"}});

  ASSERT_EQ(model.colliders().size(), 2);
  EXPECT_EQ(model.colliders().begin()->first.id, "a");
  EXPECT_EQ(model.colliders().begin()->second.id, "a");
}

TEST(ColliderPanelModelTest, SelectionUsesStableIdAcrossRefresh) {
  ColliderPanelModel model;
  model.SetColliders({{.id = "left", .name = "Z Left"}, {.id = "right", .name = "A Right"}});
  ASSERT_TRUE(model.SelectCollider("left").ok());

  model.SetColliders({{.id = "left", .name = "A Left"}, {.id = "right", .name = "Z Right"}});

  EXPECT_EQ(model.selected_collider_id(), "left");
  ASSERT_TRUE(model.BeginEditingSelectedCollider().ok());
  ASSERT_NE(model.active_collider(), nullptr);
  EXPECT_EQ(model.active_collider()->id, "left");
}

TEST(ColliderPanelModelTest, MissingSelectionCannotBeEdited) {
  ColliderPanelModel model;
  model.SetColliders({{.id = "body", .name = "Body"}});

  EXPECT_EQ(model.SelectCollider("missing").code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(model.BeginEditingSelectedCollider().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(model.has_active_collider());
}

TEST(ColliderPanelModelTest, NewColliderCreationTransitionIsExplicit) {
  ColliderPanelModel model;
  model.SetColliders({{.id = "existing", .name = "Existing"}});

  model.BeginNewCollider();
  ASSERT_TRUE(model.is_new_collider());
  EXPECT_EQ(model.active_collider()->name, "collider_1");
  ASSERT_TRUE(model.BuildSaveRequest().ok());

  ASSERT_TRUE(model.FinishCreate("generated").ok());
  EXPECT_FALSE(model.is_new_collider());
  EXPECT_EQ(model.active_collider()->id, "generated");
  EXPECT_EQ(model.selected_collider_id(), "generated");
}

TEST(ColliderPanelModelTest, ResetRestoresCatalogCopy) {
  ColliderPanelModel model;
  model.SetColliders({{.id = "body", .name = "Body", .polygons = {{{1, 2}, {3, 4}}}}});
  ASSERT_TRUE(model.BeginEditingCollider("body").ok());
  model.active_collider()->name = "Changed";
  model.active_collider()->polygons[0][0] = {99, 100};
  const std::uint64_t revision = model.active_revision();

  ASSERT_TRUE(model.ResetActiveCollider().ok());

  EXPECT_EQ(model.active_collider()->name, "Body");
  EXPECT_EQ(model.active_collider()->polygons[0][0].x, 1);
  EXPECT_GT(model.active_revision(), revision);
}

TEST(ColliderPanelModelTest, PolygonAndVertexOperationsValidateIndices) {
  ColliderPanelModel model;
  model.BeginNewCollider();
  const std::uint64_t initial_revision = model.active_revision();

  ASSERT_TRUE(model.AddPolygon().ok());
  EXPECT_GT(model.active_revision(), initial_revision);
  ASSERT_EQ(model.active_collider()->polygons.size(), 1);
  EXPECT_EQ(model.active_collider()->polygons[0].size(), 4);

  ASSERT_TRUE(model.AddVertex(0).ok());
  EXPECT_EQ(model.active_collider()->polygons[0].size(), 5);
  ASSERT_TRUE(model.DeleteVertex(0, 1).ok());
  EXPECT_EQ(model.active_collider()->polygons[0].size(), 4);
  EXPECT_EQ(model.DeleteVertex(2, 0).code(), absl::StatusCode::kOutOfRange);

  ASSERT_TRUE(model.DeletePolygon(0).ok());
  EXPECT_TRUE(model.active_collider()->polygons.empty());
  EXPECT_EQ(model.DeletePolygon(0).code(), absl::StatusCode::kOutOfRange);
}

TEST(ColliderPanelModelTest, CloseAndDeleteClearAuthoringState) {
  ColliderPanelModel model;
  model.SetColliders({{.id = "body", .name = "Body"}});
  ASSERT_TRUE(model.SelectCollider("body").ok());
  ASSERT_TRUE(model.BeginEditingSelectedCollider().ok());

  model.FinishDelete();

  EXPECT_FALSE(model.has_active_collider());
  EXPECT_FALSE(model.has_collider_selection());
  EXPECT_EQ(model.BuildSaveRequest().status().code(),
            absl::StatusCode::kFailedPrecondition);
}

// Detaching used to discard edits with no prompt. Knowing whether there is
// anything to lose is what lets it only ask when asking is warranted.
TEST(ColliderPanelModelTest, AFreshlyOpenedColliderHasNothingToLose) {
  ColliderPanelModel model;
  EXPECT_FALSE(model.has_unsaved_changes()) << "nothing is being edited";

  model.SetColliders({Collider{.id = "a", .name = "Standing"}});
  ASSERT_TRUE(model.SelectCollider("a").ok());
  ASSERT_TRUE(model.BeginEditingSelectedCollider().ok());

  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(ColliderPanelModelTest, MovingAVertexCounts) {
  ColliderPanelModel model;
  model.SetColliders({Collider{.id = "a", .name = "Standing", .polygons = {{{0, 0}, {1, 0}}}}});
  ASSERT_TRUE(model.SelectCollider("a").ok());
  ASSERT_TRUE(model.BeginEditingSelectedCollider().ok());

  model.active_collider()->polygons[0][1].x = 5.0;
  EXPECT_TRUE(model.has_unsaved_changes());

  // Dragging it back is clean again, which a latched flag could not report.
  model.active_collider()->polygons[0][1].x = 1.0;
  EXPECT_FALSE(model.has_unsaved_changes());
}

// Reset reloads from disk, so the state it leaves behind is clean by
// definition rather than by anyone remembering to say so.
TEST(ColliderPanelModelTest, ResettingIsCleanAgain) {
  ColliderPanelModel model;
  model.SetColliders({Collider{.id = "a", .name = "Standing", .polygons = {{{0, 0}, {1, 0}}}}});
  ASSERT_TRUE(model.SelectCollider("a").ok());
  ASSERT_TRUE(model.BeginEditingSelectedCollider().ok());

  ASSERT_TRUE(model.AddPolygon().ok());
  ASSERT_TRUE(model.has_unsaved_changes());

  ASSERT_TRUE(model.ResetActiveCollider().ok());
  EXPECT_FALSE(model.has_unsaved_changes());
}

TEST(ColliderPanelModelTest, SavingMakesTheCurrentStateTheCleanOne) {
  ColliderPanelModel model;
  model.BeginNewCollider();
  ASSERT_TRUE(model.AddPolygon().ok());
  ASSERT_TRUE(model.has_unsaved_changes());

  ASSERT_TRUE(model.FinishCreate("new-id").ok());
  EXPECT_FALSE(model.has_unsaved_changes());

  ASSERT_TRUE(model.AddPolygon().ok());
  ASSERT_TRUE(model.has_unsaved_changes());

  model.MarkSaved();
  EXPECT_FALSE(model.has_unsaved_changes());
}

}  // namespace
}  // namespace zebes
