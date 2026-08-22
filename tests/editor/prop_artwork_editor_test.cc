#include "editor/prop_artwork_editor/prop_artwork_editor.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_engine.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"
#include "tests/macros.h"

namespace zebes {

class PropArtworkEditorTestPeer {
 public:
  static PropArtworkEditorModel& Model(PropArtworkEditor& editor) { return editor.model_; }
  static void StartPreparation(PropArtworkEditor& editor) { editor.StartPreparation(); }
  static void StartImport(PropArtworkEditor& editor, std::string path) {
    editor.StartImport(std::move(path));
  }
  static void SelectSource(PropArtworkEditor& editor) { editor.SelectSource(); }
  static void CommitPrepared(PropArtworkEditor& editor) { editor.CommitPrepared(); }
  static void DeleteProp(PropArtworkEditor& editor) { editor.DeleteProp(); }
  static void DeleteSelectedSource(PropArtworkEditor& editor) { editor.DeleteSelectedSource(); }
  static void ClearWorkspace(PropArtworkEditor& editor) { editor.ClearWorkspace(); }
  static void OwnSessionSource(PropArtworkEditor& editor, std::string id) {
    editor.session_source_id_ = std::move(id);
  }
  static bool HasSessionSource(const PropArtworkEditor& editor) {
    return editor.session_source_id_.has_value();
  }
  static void PollWork(PropArtworkEditor& editor) { editor.PollWork(); }
  static void StartGeneration(PropArtworkEditor& editor) { editor.StartGeneration(); }
  static void CancelGeneration(PropArtworkEditor& editor) { editor.CancelGeneration(); }
  static void PollGeneration(PropArtworkEditor& editor) { editor.PollGeneration(); }
  static void AcceptCandidate(PropArtworkEditor& editor) { editor.AcceptCandidate(); }
  static void SelectGenerationProvider(PropArtworkEditor& editor, size_t index) {
    editor.SelectGenerationProvider(index);
  }
  static const PropArtworkGenerationProvider& GenerationProvider(const PropArtworkEditor& editor,
                                                                 size_t index) {
    return editor.generation_providers_.at(index);
  }
  static bool HasPendingGeneration(const PropArtworkEditor& editor) {
    return editor.pending_generation_.has_value();
  }
  static bool HasPendingWork(const PropArtworkEditor& editor) { return editor.HasPendingWork(); }
  static absl::Status WaitForWork(PropArtworkEditor& editor) {
    if (auto* pending = std::get_if<PropArtworkEditor::PendingCreation>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    if (auto* pending = std::get_if<PropArtworkEditor::PendingRegeneration>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    if (auto* pending = std::get_if<PropArtworkEditor::PendingImport>(&editor.pending_work_);
        pending != nullptr) {
      return pending->work.Wait();
    }
    return absl::FailedPreconditionError("No prop artwork work is pending");
  }
};

namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

class StubPreviewSink : public PreviewTextureSink {
 public:
  absl::StatusOr<ImTextureID> Upload(const RgbaImage&) override { return ImTextureID{0}; }
};

RgbaImage CandidatePixels(uint8_t tint);

// Completes on its first poll, so a test drives the whole request with one Run
// pass and never depends on timing. It returns exactly as many candidates as
// were asked for, because the request handle rejects a result that returns
// more than the spec requested.
class StubGenerationOperation final : public ImageGenerationOperation {
 public:
  StubGenerationOperation(std::string prompt, int candidates)
      : prompt_(std::move(prompt)), candidates_(candidates) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    ImageGenerationResult result{
        .provider = "openai",
        .model = "gpt-image-2",
        .submitted_prompt = prompt_,
        .provider_request_id = "req-1",
    };
    for (int index = 0; index < candidates_; ++index) {
      // Only the first carries a rewrite, so a test can tell the two
      // provenance spellings apart.
      result.candidates.push_back(ImageGenerationCandidate{
          .image = CandidatePixels(static_cast<uint8_t>(200 - 40 * index)),
          .revised_prompt =
              index == 0 ? std::optional<std::string>("a mossy cave boulder") : std::nullopt,
      });
    }
    return std::optional<ImageGenerationResult>(std::move(result));
  }

  void Cancel() noexcept override {}

 private:
  std::string prompt_;
  int candidates_;
};

class StubGenerationClient final : public ImageGenerationClient {
 public:
  ImageGenerationCapabilities Capabilities() const override {
    return ImageGenerationCapabilities{.maximum_candidates = 4};
  }

  const std::optional<ImageGenerationSpec>& submitted_spec() const { return submitted_spec_; }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override {
    submitted_spec_ = spec;
    return ImageGenerationRequest::Create(std::make_unique<StubGenerationOperation>(
        std::move(spec.prompt), spec.requested_candidates));
  }

 private:
  std::optional<ImageGenerationSpec> submitted_spec_;
};

class FailingGenerationOperation final : public ImageGenerationOperation {
 public:
  explicit FailingGenerationOperation(absl::Status failure) : failure_(std::move(failure)) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override { return failure_; }
  void Cancel() noexcept override {}

 private:
  absl::Status failure_;
};

class FailingGenerationClient final : public ImageGenerationClient {
 public:
  explicit FailingGenerationClient(absl::Status failure) : failure_(std::move(failure)) {}

  ImageGenerationCapabilities Capabilities() const override { return {}; }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec) override {
    return ImageGenerationRequest::Create(std::make_unique<FailingGenerationOperation>(failure_));
  }

 private:
  absl::Status failure_;
};

// A flat, opaque square: isolation is not what these generation tests are
// about, only that the pixels survive the retention path unchanged.
RgbaImage CandidatePixels(uint8_t tint) {
  RgbaImage image{.width = 16, .height = 16};
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 255);
  for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
    image.pixels[pixel * 4 + 0] = tint;
    image.pixels[pixel * 4 + 1] = tint;
    image.pixels[pixel * 4 + 2] = tint;
  }
  return image;
}

RgbaImage SourcePixels() {
  RgbaImage image{.width = 32, .height = 24};
  image.pixels.assign(static_cast<size_t>(image.width) * image.height * 4, 255);
  for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
    image.pixels[pixel * 4 + 0] = 240;
    image.pixels[pixel * 4 + 1] = 240;
    image.pixels[pixel * 4 + 2] = 240;
  }
  for (int y = 6; y < 20; ++y) {
    for (int x = 7; x < 25; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      image.pixels[offset + 0] = 72;
      image.pixels[offset + 1] = 66;
      image.pixels[offset + 2] = 62;
    }
  }
  return image;
}

class PropArtworkEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto generation_client = std::make_unique<StubGenerationClient>();
    generation_client_ = generation_client.get();
    ASSERT_OK_AND_ASSIGN(generation_, ImageGenerationEngine::Create(std::move(generation_client)));
    ASSERT_OK_AND_ASSIGN(
        editor_, PropArtworkEditor::Create({
                     .api = &api_,
                     .gui = &gui_,
                     .preview = &preview_,
                     .generation_providers = {{.name = "Stub", .engine = generation_.get()}},
                 }));
    pixels_ = SourcePixels();
    ASSERT_OK_AND_ASSIGN(const std::string digest, RgbaImageDigest(pixels_));
    source_ = SourceArtwork{
        .id = "source-1",
        .name = "Boulder source",
        .source_path = "source_art/props/source-1.png",
        .provenance =
            ImportedArtworkProvenance{
                .original_filename = "boulder.png",
                .imported_at_utc = "2026-08-16T15:04:05Z",
            },
        .width = pixels_.width,
        .height = pixels_.height,
        .content_digest = digest,
    };
    terrain_ = TerrainRecipe{
        .id = "terrain-1",
        .name = "Lucinda cave",
        .tileset_id = "tileset-1",
        .texture_id = "terrain-texture-1",
        .terrain_id = 1,
    };
    terrain_.config.tile_size = 8;
    terrain_.config.supersample = 1;

    ASSERT_OK(model().SelectSource(source_, pixels_));
    ASSERT_OK(model().AttachTerrain(terrain_));
    model().name() = "Cave boulder";
    model().settings().pipeline.isolation.minimum_subject_area = 16;
    model().settings().pipeline.composition.canvas_tiles_wide = 2;
    model().settings().pipeline.composition.canvas_tiles_high = 2;
    model().MarkInputsChanged();
  }

  void TearDown() override {
    if (temporary_path_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove(temporary_path_, ignored);
  }

  PropArtworkEditorModel& model() { return PropArtworkEditorTestPeer::Model(*editor_); }

  // Drives the engine directly rather than through a runner and a thread, so
  // the test owns every pass and a failure is a wrong result, never a timeout.
  absl::Status RunGenerationUntilEvent() {
    for (int pass = 0; pass < 4; ++pass) {
      RETURN_IF_ERROR(generation_->Run().status());
      PropArtworkEditorTestPeer::PollGeneration(*editor_);
      if (!PropArtworkEditorTestPeer::HasPendingGeneration(*editor_)) return absl::OkStatus();
    }
    return absl::DeadlineExceededError("no generation event reached the editor");
  }

  NiceMock<MockApi> api_;
  NiceMock<MockGui> gui_;
  StubPreviewSink preview_;
  StubGenerationClient* generation_client_ = nullptr;
  // Declared before the editor so it outlives the editor that submits to it,
  // matching the composition root's ordering.
  std::unique_ptr<ImageGenerationEngine> generation_;
  std::unique_ptr<PropArtworkEditor> editor_;
  RgbaImage pixels_;
  SourceArtwork source_;
  TerrainRecipe terrain_;
  std::string temporary_path_;
};

TEST_F(PropArtworkEditorTest, WorkerPreparesWithoutPublishingAndEditorCommitsAfterReview) {
  PropArtworkEditorTestPeer::OwnSessionSource(*editor_, source_.id);
  EXPECT_CALL(api_, CreateGeneratedProp(_)).Times(0);
  EXPECT_CALL(api_, DeleteSourceArtwork(_)).Times(0);

  PropArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_TRUE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);

  EXPECT_FALSE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_NE(model().prepared_creation(), nullptr);
  EXPECT_THAT(model().status(), HasSubstr("Review"));
  testing::Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, CreateGeneratedProp(_)).WillOnce(Return(std::string("recipe-1")));
  PropArtworkEditorTestPeer::CommitPrepared(*editor_);

  ASSERT_TRUE(model().active_recipe().has_value());
  EXPECT_FALSE(PropArtworkEditorTestPeer::HasSessionSource(*editor_));
  EXPECT_EQ(model().active_recipe()->id, model().prepared_creation()->recipe.id);
  EXPECT_THAT(model().status(), HasSubstr("collider-free blueprint"));
}

TEST_F(PropArtworkEditorTest, DeleteUsesTheBundleApiAndClearsTheEditor) {
  PropRecipe recipe{.id = "recipe-1",
                    .name = "Cave boulder",
                    .source_artwork_id = source_.id,
                    .terrain_recipe_id = terrain_.id};
  recipe.style = model().settings().style;
  recipe.pipeline = model().settings().pipeline;
  recipe.texture_id = "texture-1";
  recipe.sprite_id = "sprite-1";
  recipe.blueprint_id = "blueprint-1";
  recipe.expected_frame =
      SpriteFrame{.index = 0, .texture_w = 16, .texture_h = 16, .render_w = 16, .render_h = 16};
  recipe.final_pixel_digest = std::string(64, 'a');
  ASSERT_OK(model().LoadRecipe(recipe, source_, pixels_, terrain_));
  EXPECT_CALL(api_, DeleteGeneratedProp(StrEq("recipe-1"))).WillOnce(Return(absl::OkStatus()));

  PropArtworkEditorTestPeer::DeleteProp(*editor_);

  EXPECT_FALSE(model().active_recipe().has_value());
  EXPECT_FALSE(model().source().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Deleted 'Cave boulder'"));
}

TEST_F(PropArtworkEditorTest, ImportDecodesOnAWorkerThenAcceptsSourceOnTheEditorThread) {
  temporary_path_ = absl::StrCat("/tmp/zebes-prop-import-", GenerateGuid(), ".png");
  ASSERT_OK(WritePng(temporary_path_, pixels_.width, pixels_.height, pixels_.pixels));

  SourceArtwork imported;
  EXPECT_CALL(api_, CreateSourceArtwork(_, _, _))
      .WillOnce([&](std::string name, SourceArtworkProvenance provenance, const RgbaImage& image) {
        absl::StatusOr<std::string> digest = RgbaImageDigest(image);
        EXPECT_TRUE(digest.ok());
        imported = SourceArtwork{
            .id = "imported-source",
            .name = std::move(name),
            .source_path = "source_art/props/imported-source.png",
            .provenance = std::move(provenance),
            .width = image.width,
            .height = image.height,
            .content_digest = digest.ok() ? *digest : std::string(64, '0'),
        };
        return absl::StatusOr<std::string>(imported.id);
      });
  EXPECT_CALL(api_, GetSourceArtwork(StrEq("imported-source"))).WillOnce(Return(&imported));

  PropArtworkEditorTestPeer::StartImport(*editor_, temporary_path_);
  ASSERT_TRUE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);

  ASSERT_TRUE(model().source().has_value());
  EXPECT_EQ(model().source()->id, "imported-source");
  EXPECT_TRUE(PropArtworkEditorTestPeer::HasSessionSource(*editor_));
  EXPECT_THAT(model().status(), HasSubstr("Accepted source"));

  EXPECT_CALL(api_, DeleteSourceArtwork(StrEq("imported-source")))
      .WillOnce(Return(absl::OkStatus()));
  PropArtworkEditorTestPeer::ClearWorkspace(*editor_);

  EXPECT_FALSE(model().source().has_value());
  EXPECT_FALSE(PropArtworkEditorTestPeer::HasSessionSource(*editor_));
  EXPECT_THAT(model().status(), HasSubstr("workspace cleared"));
}

TEST_F(PropArtworkEditorTest, NormalShutdownDeletesAnUncommittedSessionSource) {
  PropArtworkEditorTestPeer::OwnSessionSource(*editor_, "session-source");
  EXPECT_CALL(api_, DeleteSourceArtwork(StrEq("session-source")))
      .WillOnce(Return(absl::OkStatus()));

  editor_.reset();
}

TEST_F(PropArtworkEditorTest, ClearWorkspaceDoesNotDeleteASelectedRetainedSource) {
  EXPECT_CALL(api_, DeleteSourceArtwork(_)).Times(0);

  PropArtworkEditorTestPeer::ClearWorkspace(*editor_);

  EXPECT_FALSE(model().source().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Saved prop bundles were not changed"));
}

TEST_F(PropArtworkEditorTest, SelectingARetainedSourceDiscardsTheSessionOwnedImport) {
  PropArtworkEditorTestPeer::OwnSessionSource(*editor_, source_.id);
  SourceArtwork retained = source_;
  retained.id = "retained-source";
  retained.name = "Retained tree";
  retained.source_path = "source_art/props/retained-source.png";
  model().source_to_open() = retained.id;
  EXPECT_CALL(api_, GetSourceArtwork(StrEq(retained.id))).WillOnce(Return(&retained));
  EXPECT_CALL(api_, ReadSourceArtworkPixels(StrEq(retained.id))).WillOnce(Return(pixels_));
  EXPECT_CALL(api_, DeleteSourceArtwork(StrEq(source_.id))).WillOnce(Return(absl::OkStatus()));

  PropArtworkEditorTestPeer::SelectSource(*editor_);

  ASSERT_TRUE(model().source().has_value());
  EXPECT_EQ(model().source()->id, retained.id);
  EXPECT_FALSE(PropArtworkEditorTestPeer::HasSessionSource(*editor_));
}

TEST_F(PropArtworkEditorTest, ExplicitSourceDeleteUsesTheReferenceCheckedApi) {
  EXPECT_CALL(api_, DeleteSourceArtwork(StrEq(source_.id))).WillOnce(Return(absl::OkStatus()));

  PropArtworkEditorTestPeer::DeleteSelectedSource(*editor_);

  EXPECT_FALSE(model().source().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Deleted retained source"));
}

TEST_F(PropArtworkEditorTest, RegenerationPreparesBeforeCallingTheBundleApi) {
  PropArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_NE(model().prepared_creation(), nullptr);

  const PreparedPropAsset created = *model().prepared_creation();
  model().BindCommittedRecipe(created.recipe);
  model().settings().pipeline.edge.width = 0;
  model().MarkInputsChanged();

  Texture texture = created.texture;
  Sprite sprite = created.sprite;
  EXPECT_CALL(api_, GetTexture(StrEq(created.texture.id))).WillOnce(Return(&texture));
  EXPECT_CALL(api_, ReadTexturePixels(StrEq(created.texture.id)))
      .WillOnce(Return(created.artwork.finished.image));
  EXPECT_CALL(api_, GetSprite(StrEq(created.sprite.id))).WillOnce(Return(&sprite));
  EXPECT_CALL(api_, RegenerateGeneratedProp(_)).Times(0);

  PropArtworkEditorTestPeer::StartPreparation(*editor_);
  ASSERT_TRUE(PropArtworkEditorTestPeer::HasPendingWork(*editor_));
  ASSERT_OK(PropArtworkEditorTestPeer::WaitForWork(*editor_));
  PropArtworkEditorTestPeer::PollWork(*editor_);
  ASSERT_NE(model().prepared_regeneration(), nullptr);
  testing::Mock::VerifyAndClearExpectations(&api_);

  EXPECT_CALL(api_, RegenerateGeneratedProp(_)).WillOnce(Return(absl::OkStatus()));
  PropArtworkEditorTestPeer::CommitPrepared(*editor_);

  EXPECT_THAT(model().status(), HasSubstr("without changing asset IDs"));
}

TEST_F(PropArtworkEditorTest, RefusedDeleteKeepsTheRecipeOpen) {
  PropRecipe recipe{.id = "recipe-1",
                    .name = "Cave boulder",
                    .source_artwork_id = source_.id,
                    .terrain_recipe_id = terrain_.id};
  recipe.style = model().settings().style;
  recipe.pipeline = model().settings().pipeline;
  recipe.texture_id = "texture-1";
  recipe.sprite_id = "sprite-1";
  recipe.blueprint_id = "blueprint-1";
  recipe.expected_frame =
      SpriteFrame{.index = 0, .texture_w = 16, .texture_h = 16, .render_w = 16, .render_h = 16};
  recipe.final_pixel_digest = std::string(64, 'a');
  ASSERT_OK(model().LoadRecipe(recipe, source_, pixels_, terrain_));
  EXPECT_CALL(api_, DeleteGeneratedProp(StrEq("recipe-1")))
      .WillOnce(Return(absl::FailedPreconditionError("Level 'Cave' places its blueprint")));

  PropArtworkEditorTestPeer::DeleteProp(*editor_);

  ASSERT_TRUE(model().active_recipe().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Level 'Cave'"));
}

// A generated candidate crosses the same boundary an imported PNG does: the
// manager retains the pixels, and only then does the model point at them.
TEST_F(PropArtworkEditorTest, AcceptedCandidateIsRetainedWithGeneratedProvenance) {
  model().prompt() = "a mossy boulder";
  model().SetStylePreset(PropArtworkStylePreset::kRetroExploration);
  model().SetRequestedCandidates(2, 4);

  PropArtworkEditorTestPeer::StartGeneration(*editor_);
  ASSERT_TRUE(PropArtworkEditorTestPeer::HasPendingGeneration(*editor_));
  ASSERT_OK(RunGenerationUntilEvent());

  ASSERT_TRUE(generation_client_->submitted_spec().has_value());
  EXPECT_EQ(generation_client_->submitted_spec()->prompt, "a mossy boulder");
  ASSERT_TRUE(generation_client_->submitted_spec()->instructions.has_value());
  EXPECT_THAT(*generation_client_->submitted_spec()->instructions,
              HasSubstr("transparent background"));
  EXPECT_THAT(*generation_client_->submitted_spec()->instructions,
              HasSubstr("Art direction:\n16-bit science-fiction exploration game art"));
  ASSERT_FALSE(PropArtworkEditorTestPeer::HasPendingGeneration(*editor_));
  ASSERT_TRUE(model().generation_review().has_value());
  EXPECT_EQ(model().generation_review()->candidates.size(), 2);
  EXPECT_EQ(model().generation_review()->submitted_prompt, "a mossy boulder");
  ASSERT_NE(model().SelectedCandidate(), nullptr);
  EXPECT_EQ(model().PreviewImage(), &model().SelectedCandidate()->image);

  model().SelectCandidate(1);
  SourceArtwork generated;
  EXPECT_CALL(api_, CreateSourceArtwork(_, _, _))
      .WillOnce([&](std::string name, SourceArtworkProvenance provenance, const RgbaImage& image) {
        absl::StatusOr<std::string> digest = RgbaImageDigest(image);
        EXPECT_TRUE(digest.ok());
        generated = SourceArtwork{
            .id = "generated-source",
            .name = std::move(name),
            .source_path = "source_art/props/generated-source.png",
            .provenance = std::move(provenance),
            .width = image.width,
            .height = image.height,
            .content_digest = digest.ok() ? *digest : std::string(64, '0'),
        };
        return absl::StatusOr<std::string>(generated.id);
      });
  EXPECT_CALL(api_, GetSourceArtwork(StrEq("generated-source"))).WillOnce(Return(&generated));

  PropArtworkEditorTestPeer::AcceptCandidate(*editor_);

  ASSERT_TRUE(model().source().has_value());
  EXPECT_EQ(model().source()->id, "generated-source");
  EXPECT_TRUE(PropArtworkEditorTestPeer::HasSessionSource(*editor_));
  EXPECT_FALSE(model().generation_review().has_value());
  ASSERT_TRUE(std::holds_alternative<GeneratedArtworkProvenance>(generated.provenance));
  const auto& provenance = std::get<GeneratedArtworkProvenance>(generated.provenance);
  EXPECT_EQ(provenance.provider, "openai");
  EXPECT_EQ(provenance.model, "gpt-image-2");
  EXPECT_EQ(provenance.submitted_prompt, "a mossy boulder");
  EXPECT_EQ(provenance.provider_request_id, "req-1");
  // The second candidate carried no rewrite, so neither may its provenance.
  EXPECT_FALSE(provenance.revised_prompt.has_value());
  EXPECT_FALSE(provenance.generated_at_utc.empty());
}

// The retention path is compensated: a source the model refuses must not be
// left behind as an orphan the user never asked to keep.
TEST_F(PropArtworkEditorTest, CandidateThatCannotBeRetainedIsRemovedAgain) {
  model().prompt() = "a mossy boulder";
  PropArtworkEditorTestPeer::StartGeneration(*editor_);
  ASSERT_OK(RunGenerationUntilEvent());
  ASSERT_TRUE(model().generation_review().has_value());

  EXPECT_CALL(api_, CreateSourceArtwork(_, _, _))
      .WillOnce(Return(absl::StatusOr<std::string>("generated-source")));
  EXPECT_CALL(api_, GetSourceArtwork(StrEq("generated-source")))
      .WillOnce(Return(absl::NotFoundError("no such retained source")));
  EXPECT_CALL(api_, DeleteSourceArtwork(StrEq("generated-source")))
      .WillOnce(Return(absl::OkStatus()));

  PropArtworkEditorTestPeer::AcceptCandidate(*editor_);

  // The source the editor already had is untouched: nothing replaced it, and
  // the generated one no longer exists.
  ASSERT_TRUE(model().source().has_value());
  EXPECT_EQ(model().source()->id, source_.id);
  EXPECT_FALSE(PropArtworkEditorTestPeer::HasSessionSource(*editor_));
  // The review survives so the user can accept another candidate rather than
  // regenerating after a failure that had nothing to do with the artwork.
  EXPECT_TRUE(model().generation_review().has_value());
  EXPECT_THAT(model().status(), HasSubstr("no such retained source"));
}

TEST_F(PropArtworkEditorTest, RefusedGenerationReportsTheProviderStatus) {
  model().prompt().clear();

  PropArtworkEditorTestPeer::StartGeneration(*editor_);

  EXPECT_FALSE(PropArtworkEditorTestPeer::HasPendingGeneration(*editor_));
  EXPECT_FALSE(model().generation_review().has_value());
  EXPECT_THAT(model().status(), HasSubstr("Describe the prop"));
}

TEST_F(PropArtworkEditorTest, MissingProvidersDisableOnlyGeneration) {
  editor_.reset();
  ASSERT_OK_AND_ASSIGN(editor_, PropArtworkEditor::Create({
                                    .api = &api_,
                                    .gui = &gui_,
                                    .preview = &preview_,
                                }));
  model().prompt() = "a mossy boulder";

  PropArtworkEditorTestPeer::StartGeneration(*editor_);

  EXPECT_FALSE(PropArtworkEditorTestPeer::HasPendingGeneration(*editor_));
  EXPECT_THAT(model().status(), HasSubstr("No image generation provider"));
}

TEST_F(PropArtworkEditorTest, PermanentProviderFailureAllowsFallbackSelection) {
  editor_.reset();
  generation_.reset();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationEngine> failing,
                       ImageGenerationEngine::Create(std::make_unique<FailingGenerationClient>(
                           absl::UnauthenticatedError("Codex has no active account"))));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationEngine> fallback,
                       ImageGenerationEngine::Create(std::make_unique<StubGenerationClient>()));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PropArtworkEditor> editor,
      PropArtworkEditor::Create({
          .api = &api_,
          .gui = &gui_,
          .preview = &preview_,
          .generation_providers =
              {
                  {.name = "Codex", .engine = failing.get(), .disable_after_failure = true},
                  {.name = "OpenAI API", .engine = fallback.get()},
              },
      }));
  PropArtworkEditorModel& model = PropArtworkEditorTestPeer::Model(*editor);
  model.prompt() = "a mossy boulder";

  PropArtworkEditorTestPeer::StartGeneration(*editor);
  ASSERT_OK(failing->Run().status());
  PropArtworkEditorTestPeer::PollGeneration(*editor);

  EXPECT_EQ(PropArtworkEditorTestPeer::GenerationProvider(*editor, 0).engine, nullptr);
  EXPECT_THAT(PropArtworkEditorTestPeer::GenerationProvider(*editor, 0).unavailable_reason,
              HasSubstr("no active account"));

  PropArtworkEditorTestPeer::SelectGenerationProvider(*editor, 1);
  PropArtworkEditorTestPeer::StartGeneration(*editor);
  ASSERT_OK(fallback->Run().status());
  PropArtworkEditorTestPeer::PollGeneration(*editor);

  EXPECT_TRUE(model.generation_review().has_value());
  EXPECT_THAT(model.status(), HasSubstr("Review"));
}

// Cancelling does not retire the request: the engine still owes exactly one
// event, and the editor must stay ready to collect it.
TEST_F(PropArtworkEditorTest, CancelledGenerationStaysPendingUntilItsEventArrives) {
  model().prompt() = "a mossy boulder";
  PropArtworkEditorTestPeer::StartGeneration(*editor_);

  PropArtworkEditorTestPeer::CancelGeneration(*editor_);

  EXPECT_TRUE(PropArtworkEditorTestPeer::HasPendingGeneration(*editor_));
  ASSERT_OK(RunGenerationUntilEvent());
  EXPECT_FALSE(PropArtworkEditorTestPeer::HasPendingGeneration(*editor_));
  EXPECT_FALSE(model().generation_review().has_value());
}

}  // namespace
}  // namespace zebes
