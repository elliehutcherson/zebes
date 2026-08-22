#include "editor/level_editor/blueprint_palette_model.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

std::vector<std::string> EntryIds(const BlueprintPaletteModel& model) {
  std::vector<std::string> ids;
  for (const BlueprintPaletteEntry* entry : model.FilteredEntries()) ids.push_back(entry->id);
  return ids;
}

TEST(BlueprintPaletteModelTest, FiltersCaseInsensitivelyAndSortsByNameThenId) {
  BlueprintPaletteModel model;
  const std::vector<Blueprint> blueprints = {
      {.id = "b", .name = "Crystal"},
      {.id = "z", .name = "Door"},
      {.id = "a", .name = "Crystal"},
  };
  ASSERT_OK(model.SetBlueprints(blueprints));

  EXPECT_EQ(EntryIds(model), (std::vector<std::string>{"a", "b", "z"}));
  model.SetSearchQuery("cRyS");
  EXPECT_EQ(EntryIds(model), (std::vector<std::string>{"a", "b"}));
}

TEST(BlueprintPaletteModelTest, SelectionPersistsAcrossFilteringAndCatalogRefresh) {
  BlueprintPaletteModel model;
  ASSERT_OK(model.SetBlueprints(
      std::vector<Blueprint>{{.id = "a", .name = "Crystal"}, {.id = "b", .name = "Door"}}));
  ASSERT_OK(model.ToggleSelection("a"));
  model.SetSearchQuery("Door");
  ASSERT_TRUE(model.selected_blueprint_id().has_value());
  EXPECT_EQ(*model.selected_blueprint_id(), "a");

  ASSERT_OK(model.SetBlueprints(
      std::vector<Blueprint>{{.id = "b", .name = "Door"}, {.id = "a", .name = "Renamed"}}));
  ASSERT_TRUE(model.selected_blueprint_id().has_value());
  EXPECT_EQ(*model.selected_blueprint_id(), "a");
}

TEST(BlueprintPaletteModelTest, RemovedSelectionIsCleared) {
  BlueprintPaletteModel model;
  ASSERT_OK(model.SetBlueprints(std::vector<Blueprint>{{.id = "a", .name = "Crystal"}}));
  ASSERT_OK(model.ToggleSelection("a"));
  ASSERT_OK(model.SetBlueprints(std::vector<Blueprint>{{.id = "b", .name = "Door"}}));
  EXPECT_FALSE(model.selected_blueprint_id().has_value());
}

TEST(BlueprintPaletteModelTest, PreviewMetadataUsesFirstStateSprite) {
  BlueprintPaletteModel model;
  ASSERT_OK(model.SetBlueprints(std::vector<Blueprint>{
      {.id = "with", .states = {{.sprite_id = "first"}, {.sprite_id = "second"}}},
      {.id = "without"},
  }));

  const auto entries = model.FilteredEntries();
  ASSERT_EQ(entries.size(), 2u);
  ASSERT_TRUE(entries[0]->preview.sprite_id.has_value());
  EXPECT_EQ(*entries[0]->preview.sprite_id, "first");
  EXPECT_FALSE(entries[1]->preview.sprite_id.has_value());
}

TEST(BlueprintPaletteModelTest, RejectsInvalidCatalogAndUnknownSelection) {
  BlueprintPaletteModel model;
  EXPECT_EQ(model.SetBlueprints(std::vector<Blueprint>{{.id = ""}}).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(model.SetBlueprints(std::vector<Blueprint>{{.id = "a"}, {.id = "a"}}).code(),
            absl::StatusCode::kInvalidArgument);
  ASSERT_OK(model.SetBlueprints(std::vector<Blueprint>{{.id = "a"}}));
  EXPECT_EQ(model.ToggleSelection("missing").code(), absl::StatusCode::kNotFound);
}

TEST(BlueprintPaletteModelTest, ClickingSelectedEntryTogglesItOff) {
  BlueprintPaletteModel model;
  ASSERT_OK(model.SetBlueprints(std::vector<Blueprint>{{.id = "a"}}));
  ASSERT_OK(model.ToggleSelection("a"));
  ASSERT_OK(model.ToggleSelection("a"));
  EXPECT_FALSE(model.selected_blueprint_id().has_value());
}

}  // namespace
}  // namespace zebes
