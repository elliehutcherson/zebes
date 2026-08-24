#include "editor/prop_artwork_editor/prop_artwork_controls_panel.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "common/image_digest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "tests/editor/mock_gui.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class PropArtworkControlsPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(panel_, PropArtworkControlsPanel::Create(&gui_));
    ON_CALL(gui_, CreateScopedCombo(_, _, _))
        .WillByDefault(Invoke([this](const char* label, const char* preview, ImGuiComboFlags) {
          return ScopedCombo(&gui_, label, preview);
        }));
    ON_CALL(gui_, BeginCombo(_, _, _)).WillByDefault(Return(false));
    ON_CALL(gui_, Button(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, CollapsingHeader(_, _)).WillByDefault(Return(false));
    ON_CALL(gui_, CreateScopedStyleColor(_, An<const ImVec4&>()))
        .WillByDefault(Invoke([this](ImGuiCol index, const ImVec4& color) {
          return ScopedStyleColor(&gui_, index, color);
        }));
  }

  void ClickOnly(const std::string& label) {
    ON_CALL(gui_, Button(_, _)).WillByDefault(Invoke([label](const char* candidate, const ImVec2&) {
      return label == candidate;
    }));
  }

  static TerrainRecipe Terrain(int tile_size) {
    TerrainRecipe recipe{.id = "terrain-1", .name = "Cave"};
    recipe.config.tile_size = tile_size;
    return recipe;
  }

  // Builds the review the editor holds after a finished generation, so the
  // panel can be asked what it offers for one without an engine.
  static ImageGenerationReview Review(size_t candidates) {
    ImageGenerationReview review{
        .provider = "openai",
        .model = "gpt-image-2",
        .submitted_prompt = "a mossy boulder",
        .provider_request_id = "req-1",
        .generated_at_utc = "2026-08-19T12:00:00Z",
    };
    for (size_t index = 0; index < candidates; ++index) {
      review.candidates.push_back(ImageGenerationCandidate{
          .image = RgbaImage{.width = 1, .height = 1, .pixels = {10, 20, 30, 255}},
      });
    }
    return review;
  }

  NiceMock<MockGui> gui_;
  std::unique_ptr<PropArtworkControlsPanel> panel_;
  PropArtworkEditorModel model_;
  PropGenerationStatus generation_{
      .providers = {{.name = "Stub", .available = true}},
      .capabilities = {.maximum_candidates = 4},
  };
};

TEST_F(PropArtworkControlsPanelTest, RefreshUsesTheCurrentTerrainRecipeSnapshot) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  ClickOnly("Refresh style##PropArtwork");

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(16)}, generation_));

  EXPECT_EQ(model_.settings().style.tile_size, 16);
}

TEST_F(PropArtworkControlsPanelTest, DetachKeepsTheResolvedStyleSnapshot) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  const PropArtworkStyle style = model_.settings().style;
  ClickOnly("Detach style##PropArtwork");

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(8)}, generation_));

  EXPECT_FALSE(model_.terrain_recipe().has_value());
  EXPECT_FALSE(model_.settings().terrain_recipe_id.has_value());
  EXPECT_TRUE(model_.has_style());
  EXPECT_EQ(model_.settings().style.palette.colors, style.palette.colors);
}

TEST_F(PropArtworkControlsPanelTest, DeletingARetainedSourceRequiresConfirmation) {
  RgbaImage pixels{.width = 1, .height = 1, .pixels = {20, 30, 40, 255}};
  ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(pixels));
  SourceArtwork source{
      .id = "source-1",
      .name = "Boulder source",
      .source_path = "source_art/source-1.png",
      .provenance = ImportedArtworkProvenance{.original_filename = "boulder.png",
                                              .imported_at_utc = "2026-08-17T12:00:00Z"},
      .width = 1,
      .height = 1,
      .content_digest = digest,
  };
  ASSERT_OK(model_.SelectSource(source, pixels));

  ClickOnly("Delete source##PropArtworkSource");
  ASSERT_OK_AND_ASSIGN(PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {source}, {}, generation_));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kNone);

  ClickOnly("Confirm##PropArtworkSource");
  ASSERT_OK_AND_ASSIGN(action, panel_->Render(model_, {source}, {}, generation_));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kDeleteSource);
}

TEST_F(PropArtworkControlsPanelTest, FreeAttachmentStartsAtTheCanvasCenter) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  ON_CALL(gui_, CollapsingHeader(_, _)).WillByDefault(Return(true));
  ON_CALL(gui_, BeginCombo(_, _, _))
      .WillByDefault(Invoke([](const char* label, const char*, ImGuiComboFlags) {
        return std::string(label) == "Attachment##PropArtwork";
      }));
  ON_CALL(gui_, Selectable(_, An<bool>(), _, _))
      .WillByDefault(Invoke([](const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
        return std::string(label) == "Free / background";
      }));

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(8)}, generation_));

  const PropAttachmentConfig& attachment = model_.settings().pipeline.composition.attachment;
  EXPECT_EQ(attachment.mode, PropAttachmentMode::kFree);
  ASSERT_TRUE(attachment.free_anchor.has_value());
  EXPECT_EQ(attachment.free_anchor->x, 12);
  EXPECT_EQ(attachment.free_anchor->y, 8);
}

TEST_F(PropArtworkControlsPanelTest, ReselectingFreeAttachmentKeepsTheAuthoredAnchor) {
  ASSERT_OK(model_.AttachTerrain(Terrain(8)));
  model_.settings().pipeline.composition.attachment = PropAttachmentConfig{
      .mode = PropAttachmentMode::kFree,
      .free_anchor = PropFreeAnchor{.x = 2, .y = 3},
  };
  ON_CALL(gui_, CollapsingHeader(_, _)).WillByDefault(Return(true));
  ON_CALL(gui_, BeginCombo(_, _, _))
      .WillByDefault(Invoke([](const char* label, const char*, ImGuiComboFlags) {
        return std::string(label) == "Attachment##PropArtwork";
      }));
  ON_CALL(gui_, Selectable(_, An<bool>(), _, _))
      .WillByDefault(Invoke([](const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
        return std::string(label) == "Free / background";
      }));

  ASSERT_OK(panel_->Render(model_, {}, {Terrain(8)}, generation_));

  ASSERT_TRUE(model_.settings().pipeline.composition.attachment.free_anchor.has_value());
  EXPECT_EQ(model_.settings().pipeline.composition.attachment.free_anchor->x, 2);
  EXPECT_EQ(model_.settings().pipeline.composition.attachment.free_anchor->y, 3);
}

TEST_F(PropArtworkControlsPanelTest, GenerateReportsTheIntentOnceAPromptExists) {
  model_.prompt() = "a mossy boulder";
  ClickOnly("Generate##PropArtwork");

  ASSERT_OK_AND_ASSIGN(const PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {}, {}, generation_));

  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kGenerate);
}

TEST_F(PropArtworkControlsPanelTest, RendersLargeEditableGenerationPrompts) {
  ON_CALL(gui_, GetTextLineHeightWithSpacing()).WillByDefault(Return(20.0f));

  EXPECT_CALL(gui_, InputTextMultiline(testing::StrEq("##PropArtworkPrompt"), &model_.prompt(),
                                       testing::Truly([](const ImVec2& size) {
                                         return size.x == -1.0f && size.y == 100.0f;
                                       }),
                                       0, nullptr, nullptr));
  EXPECT_CALL(gui_, InputTextMultiline(testing::StrEq("##PropArtworkSystemPrompt"),
                                       &model_.generation_instructions(),
                                       testing::Truly([](const ImVec2& size) {
                                         return size.x == -1.0f && size.y == 140.0f;
                                       }),
                                       0, nullptr, nullptr));
  EXPECT_CALL(gui_,
              InputTextMultiline(testing::StrEq("##PropArtworkStyleGuidance"),
                                 &model_.style_guidance(), testing::Truly([](const ImVec2& size) {
                                   return size.x == -1.0f && size.y == 80.0f;
                                 }),
                                 0, nullptr, nullptr));

  ASSERT_OK(panel_->Render(model_, {}, {}, generation_));
  EXPECT_THAT(model_.generation_instructions(), HasSubstr("transparent background"));
  EXPECT_THAT(model_.generation_instructions(), HasSubstr("Do not add scenery"));
}

TEST_F(PropArtworkControlsPanelTest, StylePresetPopulatesEditableGuidance) {
  ON_CALL(gui_, BeginCombo(_, _, _))
      .WillByDefault(Invoke([](const char* label, const char*, ImGuiComboFlags) {
        return std::string(label) == "Style preset##PropArtworkGeneration";
      }));
  ON_CALL(gui_, Selectable(_, An<bool>(), _, _))
      .WillByDefault(Invoke([](const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
        return std::string(label) == "Retro exploration";
      }));

  ASSERT_OK(panel_->Render(model_, {}, {}, generation_));

  EXPECT_EQ(model_.style_preset(), PropArtworkStylePreset::kRetroExploration);
  EXPECT_THAT(model_.style_guidance(), HasSubstr("16-bit science-fiction exploration game art"));
  EXPECT_THAT(model_.style_guidance(), HasSubstr("strong silhouette"));
}

TEST_F(PropArtworkControlsPanelTest, EditingPresetGuidanceSwitchesToCustom) {
  model_.SetStylePreset(PropArtworkStylePreset::kCleanCartoon);
  ON_CALL(gui_, InputTextMultiline(_, _, _, _, _, _))
      .WillByDefault(Invoke([](const char* label, std::string* value, const ImVec2&,
                               ImGuiInputTextFlags, ImGuiInputTextCallback, void*) {
        if (std::string(label) != "##PropArtworkStyleGuidance") return false;
        *value = "Ink-wash adventure game art with a restrained blue palette.";
        return true;
      }));

  ASSERT_OK(panel_->Render(model_, {}, {}, generation_));

  EXPECT_EQ(model_.style_preset(), PropArtworkStylePreset::kCustom);
  EXPECT_EQ(model_.style_guidance(), "Ink-wash adventure game art with a restrained blue palette.");
}

TEST_F(PropArtworkControlsPanelTest, ProviderSelectionIsReturnedToTheEditor) {
  generation_.providers.push_back({.name = "OpenAI API", .available = true});
  ON_CALL(gui_, BeginCombo(_, _, _))
      .WillByDefault(Invoke([](const char* label, const char*, ImGuiComboFlags) {
        return std::string(label) == "Provider##PropArtworkGeneration";
      }));
  ON_CALL(gui_, Selectable(_, An<bool>(), _, _))
      .WillByDefault(Invoke([](const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
        return std::string(label) == "OpenAI API";
      }));

  ASSERT_OK_AND_ASSIGN(const PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {}, {}, generation_));

  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kSelectGenerationProvider);
  EXPECT_EQ(generation_.selected_provider, 1);
}

TEST_F(PropArtworkControlsPanelTest, UnavailableProviderDisablesGenerationAndExplainsWhy) {
  generation_.providers[0] = {
      .name = "Codex",
      .available = false,
      .unavailable_reason = "Codex has no active account",
  };
  model_.prompt() = "a mossy boulder";
  ClickOnly("Generate##PropArtwork");

  ASSERT_OK_AND_ASSIGN(const PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {}, {}, generation_));

  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kNone);
  EXPECT_THAT(gui_.wrapped_text(), Contains(HasSubstr("Codex has no active account")));
}

// A running request replaces the whole section, so the only thing a second
// click can reach is the cancel it is meant to reach.
TEST_F(PropArtworkControlsPanelTest, AnInFlightRequestOffersOnlyCancel) {
  model_.prompt() = "a mossy boulder";
  generation_.in_flight = true;
  ClickOnly("Generate##PropArtwork");

  ASSERT_OK_AND_ASSIGN(PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {}, {}, generation_));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kNone);

  ClickOnly("Cancel generation##PropArtwork");
  ASSERT_OK_AND_ASSIGN(action, panel_->Render(model_, {}, {}, generation_));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kCancelGeneration);
}

TEST_F(PropArtworkControlsPanelTest, CandidateNavigationWrapsAcrossTheGeneratedSet) {
  ImageGenerationReview review = Review(3);
  generation_.review = &review;
  ClickOnly("Previous##PropArtworkCandidate");

  ASSERT_OK_AND_ASSIGN(const PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {}, {}, generation_));

  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kSelectCandidate);
  EXPECT_EQ(generation_.selected_candidate, 2);
}

TEST_F(PropArtworkControlsPanelTest, ReviewOffersAcceptAndDiscard) {
  ImageGenerationReview review = Review(1);
  generation_.review = &review;

  ClickOnly("Accept candidate##PropArtwork");
  ASSERT_OK_AND_ASSIGN(PropArtworkControlsPanel::Action action,
                       panel_->Render(model_, {}, {}, generation_));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kAcceptCandidate);

  ClickOnly("Discard##PropArtworkCandidate");
  ASSERT_OK_AND_ASSIGN(action, panel_->Render(model_, {}, {}, generation_));
  EXPECT_EQ(action, PropArtworkControlsPanel::Action::kDiscardCandidates);
}

// The provider's ceiling is whatever the running adapter reports, not what it
// reported when the number was chosen.
TEST_F(PropArtworkControlsPanelTest, RequestedCandidatesAreClampedToTheProviderCeiling) {
  model_.SetRequestedCandidates(4, 4);
  generation_.capabilities.maximum_candidates = 2;

  ASSERT_OK(panel_->Render(model_, {}, {}, generation_));

  EXPECT_EQ(model_.requested_candidates(), 2);
}

}  // namespace
}  // namespace zebes
