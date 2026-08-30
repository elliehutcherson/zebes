#include "scripts/pose_conditioned_animation_batch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api_mock.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utils.h"
#include "generation/image_generation.h"
#include "generation/image_generation_service.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

constexpr int kCellWidth = 12;
constexpr int kCellHeight = 12;
constexpr int kColumns = 6;
constexpr int kRows = 2;
constexpr int kGridX = 2;
constexpr int kGridY = 1;
constexpr int kColumnGap = 1;
constexpr int kRowGap = 1;
constexpr RgbaColor kMatte{255, 0, 255, 255};
constexpr RgbaColor kBody{0x10, 0x13, 0x1C, 255};
constexpr RgbaColor kHighlight{0xD8, 0xD6, 0xC9, 255};

void SetPixel(RgbaImage& image, int x, int y, RgbaColor color) {
  const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
  image.pixels[offset + 0] = color.r;
  image.pixels[offset + 1] = color.g;
  image.pixels[offset + 2] = color.b;
  image.pixels[offset + 3] = color.a;
}

void PaintRect(RgbaImage& image, int left, int top, int width, int height, RgbaColor color) {
  for (int y = top; y < top + height; ++y) {
    for (int x = left; x < left + width; ++x) SetPixel(image, x, y, color);
  }
}

RgbaImage SolidImage(int width, int height, RgbaColor color) {
  RgbaImage image{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4),
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) SetPixel(image, x, y, color);
  }
  return image;
}

RgbaImage BuildPoseSheet() {
  // Deliberate right/bottom margins prove that guide crop geometry is not
  // conflated with the gapless generated-sheet processing contract.
  RgbaImage sheet = SolidImage(kGridX + kColumns * kCellWidth + (kColumns - 1) * kColumnGap + 3,
                               kGridY + kRows * kCellHeight + kRowGap + 2, kMatte);
  for (int index = 0; index < kColumns * kRows; ++index) {
    const int cell_left = kGridX + (index % kColumns) * (kCellWidth + kColumnGap);
    const int cell_top = kGridY + (index / kColumns) * (kCellHeight + kRowGap);
    const int subject_left = 1 + (index % kColumns);
    PaintRect(sheet, cell_left + subject_left, cell_top + 9, 2, 2, kBody);
    PaintRect(sheet, cell_left + subject_left, cell_top + 8, 1, 1, kHighlight);
  }
  return sheet;
}

struct FakeState {
  std::mutex mutex;
  std::vector<ImageGenerationSpec> requests;
  std::optional<size_t> failing_call;
  std::optional<size_t> wrong_size_call;
  int output_width = kCellWidth;
  int output_height = kCellHeight;
  std::string result_provider = "fake";
};

class ScriptedFakeOperation final : public ImageGenerationOperation {
 public:
  ScriptedFakeOperation(ImageGenerationSpec spec, size_t call_index,
                        std::shared_ptr<FakeState> state)
      : spec_(std::move(spec)), call_index_(call_index), state_(std::move(state)) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    if (finished_) return std::nullopt;
    finished_ = true;
    if (state_->failing_call == call_index_) {
      return absl::UnavailableError("scripted fake transport failure");
    }
    const RgbaImage& pose = spec_.references.at(1).image;
    RgbaImage output = SolidImage(state_->output_width, state_->output_height, kMatte);
    for (int y = 0; y < output.height; ++y) {
      const int source_y = y * pose.height / output.height;
      for (int x = 0; x < output.width; ++x) {
        const int source_x = x * pose.width / output.width;
        const size_t source = (static_cast<size_t>(source_y) * pose.width + source_x) * 4;
        const size_t destination = (static_cast<size_t>(y) * output.width + x) * 4;
        std::copy_n(pose.pixels.begin() + static_cast<ptrdiff_t>(source), 4,
                    output.pixels.begin() + static_cast<ptrdiff_t>(destination));
      }
    }
    if (state_->wrong_size_call == call_index_) {
      output = SolidImage(output.width + 1, output.height, kMatte);
    }
    return std::optional<ImageGenerationResult>(ImageGenerationResult{
        .provider = state_->result_provider,
        .model = "zebes-fake-v1",
        .submitted_prompt = spec_.prompt,
        .provider_request_id = "fake-request-" + std::to_string(call_index_),
        .candidates = {{.image = std::move(output), .revised_prompt = std::nullopt}},
    });
  }

  void Cancel() noexcept override { finished_ = true; }

 private:
  ImageGenerationSpec spec_;
  size_t call_index_ = 0;
  std::shared_ptr<FakeState> state_;
  bool finished_ = false;
};

class ScriptedFakeClient final : public ImageGenerationClient {
 public:
  explicit ScriptedFakeClient(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}

  ImageGenerationCapabilities Capabilities() const override {
    return {
        .maximum_candidates = 1,
        .supports_negative_prompt = false,
        .supports_transparency = true,
        .maximum_reference_images = 2,
        .maximum_reference_pixels = 1024 * 1024,
    };
  }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override {
    size_t call_index = 0;
    {
      std::lock_guard lock(state_->mutex);
      call_index = state_->requests.size();
      state_->requests.push_back(spec);
    }
    return ImageGenerationRequest::Create(
        std::make_unique<ScriptedFakeOperation>(std::move(spec), call_index, state_));
  }

 private:
  std::shared_ptr<FakeState> state_;
};

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  if (!stream.is_open()) return absl::InternalError("could not open test JSON");
  stream << value.dump(2) << '\n';
  if (!stream.good()) return absl::InternalError("could not write test JSON");
  return absl::OkStatus();
}

absl::StatusOr<nlohmann::json> ReadJson(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) return absl::NotFoundError("could not open test JSON");
  nlohmann::json value;
  stream >> value;
  return value;
}

struct InputKit {
  std::filesystem::path manifest;
  RgbaImage pose_sheet;
};

absl::StatusOr<InputKit> WriteInputKit(const std::filesystem::path& root, int output_width = 12,
                                       int output_height = 12, std::string provider = "fake") {
  const RgbaImage identity = SolidImage(3, 2, kBody);
  RgbaImage pose_sheet = BuildPoseSheet();
  RETURN_IF_ERROR(
      WritePng((root / "identity.png").string(), identity.width, identity.height, identity.pixels));
  RETURN_IF_ERROR(WritePng((root / "pose-sheet.png").string(), pose_sheet.width, pose_sheet.height,
                           pose_sheet.pixels));
  nlohmann::json planted = nlohmann::json::array();
  for (int index = 0; index < 12; ++index) planted.push_back(true);
  RETURN_IF_ERROR(
      WriteJson(root / "animation-run.json", {
                                                 {"schema_version", 1},
                                                 {"clip", "locomotion-right"},
                                                 {"sheet",
                                                  {{"grid_x", 0},
                                                   {"grid_y", 0},
                                                   {"cell_width", output_width},
                                                   {"cell_height", output_height},
                                                   {"column_gap", 0},
                                                   {"row_gap", 0},
                                                   {"columns", 6},
                                                   {"rows", 2}}},
                                                 {"timing", {{"uniform_frames_per_cycle", 3}}},
                                                 {"planted_frames", std::move(planted)},
                                             }));
  const int aspect_divisor = std::gcd(output_width, output_height);
  const std::filesystem::path manifest = root / "experiment.json";
  RETURN_IF_ERROR(WriteJson(
      manifest,
      {
          {"schema_version", 1},
          {"experiment_id", "pose-conditioned-run-v1"},
          {"animation_run_manifest", "animation-run.json"},
          {"identity_source", {{"path", "identity.png"}}},
          {"pose_sheet_source", {{"path", "pose-sheet.png"}}},
          {"pose_sheet_layout",
           {{"grid_x", kGridX},
            {"grid_y", kGridY},
            {"cell_width", kCellWidth},
            {"cell_height", kCellHeight},
            {"column_gap", kColumnGap},
            {"row_gap", kRowGap},
            {"columns", kColumns},
            {"rows", kRows}}},
          {"generation",
           {{"provider", std::move(provider)},
            {"model", "zebes-fake-v1"},
            {"instructions", "Keep identity and use the labelled references."},
            {"prompt", "Render the locked run character."},
            {"negative_requirements", "No sheet, wireframe, text, or copied identity board."},
            {"target_aspect",
             {{"width", output_width / aspect_divisor},
              {"height", output_height / aspect_divisor}}},
            {"transparency", "prefer-transparent"},
            {"expected_output", {{"width", output_width}, {"height", output_height}}}}},
      }));
  return InputKit{.manifest = manifest, .pose_sheet = std::move(pose_sheet)};
}

absl::StatusOr<std::filesystem::path> RunPilotAndApprove(const std::filesystem::path& root,
                                                         MockApi& api, int output_width = 12,
                                                         int output_height = 12) {
  ASSIGN_OR_RETURN(const InputKit inputs, WriteInputKit(root, output_width, output_height));
  auto state = std::make_shared<FakeState>();
  state->output_width = output_width;
  state->output_height = output_height;
  ASSIGN_OR_RETURN(std::unique_ptr<ImageGenerationService> service,
                   ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));
  const std::filesystem::path pilot = root / "pilot";
  RETURN_IF_ERROR(
      RunPoseConditionedAnimationBatch(api, *service,
                                       {.manifest_path = inputs.manifest,
                                        .output_path = pilot,
                                        .phase = PoseConditionedAnimationPhase::kPilot}));
  ASSIGN_OR_RETURN(const nlohmann::json pilot_manifest, ReadJson(pilot / "manifest.json"));
  const std::filesystem::path approval = root / "pilot-approval.json";
  RETURN_IF_ERROR(WriteJson(approval, {
                                          {"schema_version", 1},
                                          {"decision", "approved"},
                                          {"reviewer", "test-reviewer"},
                                          {"reviewed_at_utc", "2026-08-30T12:00:00Z"},
                                          {"pilot_manifest", "pilot/manifest.json"},
                                          {"pilot_run_id", pilot_manifest.at("run_id")},
                                          {"checks",
                                           {{"frame_0_fresh_single_render", true},
                                            {"frame_0_identity_preserved", true},
                                            {"frame_0_pose_obeyed", true},
                                            {"frame_6_fresh_single_render", true},
                                            {"frame_6_identity_preserved", true},
                                            {"frame_6_pose_obeyed", true}}},
                                      }));
  return approval;
}

class PoseConditionedAnimationBatchTest : public ::testing::Test {
 protected:
  PoseConditionedAnimationBatchTest()
      : root_(std::filesystem::temp_directory_path() /
              std::filesystem::path("zebes-pose-batch-" + GenerateGuid())) {}

  ~PoseConditionedAnimationBatchTest() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  std::filesystem::path root_;
  MockApi api_;
};

TEST_F(PoseConditionedAnimationBatchTest, MapsTheCodexAdapterToCanonicalResultProvenance) {
  ASSERT_OK_AND_ASSIGN(const InputKit inputs, WriteInputKit(root_, 12, 12, "codex"));
  auto state = std::make_shared<FakeState>();
  state->result_provider = "openai-codex";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  ASSERT_OK_AND_ASSIGN(const PoseConditionedAnimationProviderConfig provider,
                       LoadPoseConditionedAnimationProviderConfig(inputs.manifest));
  ASSERT_OK(RunPoseConditionedAnimationBatch(api_, *service,
                                             {.manifest_path = inputs.manifest,
                                              .output_path = root_ / "codex-provider-mapping",
                                              .phase = PoseConditionedAnimationPhase::kPilot}));
  ASSERT_OK_AND_ASSIGN(const nlohmann::json evidence,
                       ReadJson(root_ / "codex-provider-mapping/manifest.json"));

  EXPECT_EQ(provider.provider, "codex");
  EXPECT_EQ(provider.model, "zebes-fake-v1");
  EXPECT_EQ(evidence.at("locked_request").at("generation").at("result_provider"), "openai-codex");
  EXPECT_EQ(evidence.at("requests")[0].at("provider"), "openai-codex");
}

TEST_F(PoseConditionedAnimationBatchTest, PilotSubmitsOnlyFreshFramesZeroAndSix) {
  ASSERT_OK_AND_ASSIGN(const InputKit inputs, WriteInputKit(root_));
  auto state = std::make_shared<FakeState>();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  ASSERT_OK(RunPoseConditionedAnimationBatch(api_, *service,
                                             {.manifest_path = inputs.manifest,
                                              .output_path = root_ / "pilot",
                                              .phase = PoseConditionedAnimationPhase::kPilot}));

  std::lock_guard lock(state->mutex);
  ASSERT_EQ(state->requests.size(), 2);
  for (const ImageGenerationSpec& request : state->requests) {
    ASSERT_EQ(request.references.size(), 2);
    EXPECT_EQ(request.references[0].role, ImageGenerationReferenceRole::kSubjectIdentity);
    EXPECT_EQ(request.references[1].role, ImageGenerationReferenceRole::kPose);
    EXPECT_FALSE(request.negative_prompt.has_value());
    ASSERT_TRUE(request.instructions.has_value());
    EXPECT_NE(request.instructions->find("Negative requirements"), std::string::npos);
  }
  ASSERT_OK_AND_ASSIGN(const nlohmann::json manifest, ReadJson(root_ / "pilot/manifest.json"));
  EXPECT_EQ(manifest.at("phase"), "pilot");
  EXPECT_EQ(manifest.at("classification"), "non-candidate-evidence");
  EXPECT_FALSE(manifest.at("animation_candidate").get<bool>());
  ASSERT_EQ(manifest.at("requests").size(), 2);
  EXPECT_EQ(manifest.at("requests")[0].at("frame_index"), 0);
  EXPECT_EQ(manifest.at("requests")[1].at("frame_index"), 6);
  ASSERT_EQ(manifest.at("locked_request").at("pose_sheet").at("cells").size(), 12);
  EXPECT_EQ(manifest.at("locked_request").at("pose_sheet").at("width"), inputs.pose_sheet.width);
}

TEST_F(PoseConditionedAnimationBatchTest, ApprovedBatchSubmitsAndAssemblesAllTwelveInOrder) {
  ASSERT_OK_AND_ASSIGN(const std::filesystem::path approval, RunPilotAndApprove(root_, api_));
  auto state = std::make_shared<FakeState>();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  ASSERT_OK(RunPoseConditionedAnimationBatch(api_, *service,
                                             {.manifest_path = root_ / "experiment.json",
                                              .output_path = root_ / "batch",
                                              .phase = PoseConditionedAnimationPhase::kBatch,
                                              .pilot_approval_path = approval}));

  std::lock_guard lock(state->mutex);
  ASSERT_EQ(state->requests.size(), 12);
  ASSERT_OK_AND_ASSIGN(const nlohmann::json manifest, ReadJson(root_ / "batch/manifest.json"));
  EXPECT_EQ(manifest.at("phase"), "batch");
  EXPECT_EQ(manifest.at("status"), "complete");
  EXPECT_EQ(manifest.at("classification"), "animation-candidate");
  EXPECT_TRUE(manifest.at("animation_candidate").get<bool>());
  ASSERT_EQ(manifest.at("requests").size(), 12);
  const nlohmann::json& cells = manifest.at("locked_request").at("pose_sheet").at("cells");
  for (size_t index = 0; index < state->requests.size(); ++index) {
    EXPECT_NE(state->requests[index].prompt.find("Animation frame " + std::to_string(index)),
              std::string::npos);
    ASSERT_OK_AND_ASSIGN(const std::string pose_digest,
                         RgbaImageDigest(state->requests[index].references[1].image));
    EXPECT_EQ(pose_digest, cells[index].at("rgba_sha256").get<std::string>());
    EXPECT_EQ(manifest.at("requests")[index].at("frame_index"), index);
  }
  EXPECT_EQ(manifest.at("processing").at("packed_rgba_sha256").get<std::string>().size(), 64);
  ASSERT_OK_AND_ASSIGN(const RgbaImage assembled,
                       ReadPng((root_ / "batch/assembled-source-sheet.png").string()));
  EXPECT_EQ(assembled.width, 72);
  EXPECT_EQ(assembled.height, 24);
  ASSERT_OK_AND_ASSIGN(const RgbaImage packed,
                       ReadPng((root_ / "batch/processed/packed-texture.png").string()));
  EXPECT_EQ(packed.width, 576);
  EXPECT_EQ(packed.height, 44);
}

TEST_F(PoseConditionedAnimationBatchTest,
       ApprovedBatchProcessesAllFramesBeyondGenericDimensionAndPixelLimits) {
  constexpr int kOutputSize = 1184;
  constexpr int kAssembledWidth = kColumns * kOutputSize;
  constexpr int kAssembledHeight = kRows * kOutputSize;
  constexpr int64_t kAssembledPixels = static_cast<int64_t>(kAssembledWidth) * kAssembledHeight;
  static_assert(kAssembledWidth > 2048);
  static_assert(kAssembledPixels > 16 * 1024 * 1024);
  static_assert(kAssembledPixels < 64 * 1024 * 1024);
  ASSERT_OK_AND_ASSIGN(const std::filesystem::path approval,
                       RunPilotAndApprove(root_, api_, kOutputSize, kOutputSize));
  auto state = std::make_shared<FakeState>();
  state->output_width = kOutputSize;
  state->output_height = kOutputSize;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  ASSERT_OK(RunPoseConditionedAnimationBatch(api_, *service,
                                             {.manifest_path = root_ / "experiment.json",
                                              .output_path = root_ / "wide-batch",
                                              .phase = PoseConditionedAnimationPhase::kBatch,
                                              .pilot_approval_path = approval}));

  std::lock_guard lock(state->mutex);
  EXPECT_EQ(state->requests.size(), 12);
  ASSERT_OK_AND_ASSIGN(const nlohmann::json manifest, ReadJson(root_ / "wide-batch/manifest.json"));
  EXPECT_EQ(manifest.at("status"), "complete");
  EXPECT_EQ(manifest.at("classification"), "animation-candidate");
  EXPECT_TRUE(manifest.at("animation_candidate").get<bool>());
  EXPECT_EQ(manifest.at("requests").size(), 12);
  EXPECT_TRUE(manifest.at("processing").at("passed").get<bool>());
  EXPECT_EQ(manifest.at("processing").at("frames").size(), 12);
  EXPECT_EQ(manifest.at("processing").at("sprite_frames").size(), 12);
  ASSERT_OK_AND_ASSIGN(const RgbaImage assembled,
                       ReadPng((root_ / "wide-batch/assembled-source-sheet.png").string()));
  EXPECT_EQ(assembled.width, kAssembledWidth);
  EXPECT_EQ(assembled.height, kAssembledHeight);
}

TEST_F(PoseConditionedAnimationBatchTest, BatchGateRefusesBeforeAnyProviderRequest) {
  ASSERT_OK_AND_ASSIGN(const InputKit inputs, WriteInputKit(root_));
  auto state = std::make_shared<FakeState>();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  const absl::Status status =
      RunPoseConditionedAnimationBatch(api_, *service,
                                       {.manifest_path = inputs.manifest,
                                        .output_path = root_ / "batch",
                                        .phase = PoseConditionedAnimationPhase::kBatch});

  EXPECT_TRUE(absl::IsFailedPrecondition(status));
  std::lock_guard lock(state->mutex);
  EXPECT_TRUE(state->requests.empty());
  EXPECT_FALSE(std::filesystem::exists(root_ / "batch"));
}

TEST_F(PoseConditionedAnimationBatchTest, ReviewedPilotGateRejectsChangedRequestShape) {
  ASSERT_OK_AND_ASSIGN(const std::filesystem::path approval, RunPilotAndApprove(root_, api_));
  ASSERT_OK_AND_ASSIGN(nlohmann::json experiment, ReadJson(root_ / "experiment.json"));
  experiment["generation"]["prompt"] = "A changed unpiloted request shape.";
  ASSERT_OK(WriteJson(root_ / "experiment.json", experiment));
  auto state = std::make_shared<FakeState>();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  const absl::Status status =
      RunPoseConditionedAnimationBatch(api_, *service,
                                       {.manifest_path = root_ / "experiment.json",
                                        .output_path = root_ / "changed-batch",
                                        .phase = PoseConditionedAnimationPhase::kBatch,
                                        .pilot_approval_path = approval});

  EXPECT_TRUE(absl::IsFailedPrecondition(status));
  std::lock_guard lock(state->mutex);
  EXPECT_TRUE(state->requests.empty());
  EXPECT_FALSE(std::filesystem::exists(root_ / "changed-batch"));
}

TEST_F(PoseConditionedAnimationBatchTest, PartialBatchPublishesOnlyNonCandidateEvidence) {
  ASSERT_OK_AND_ASSIGN(const std::filesystem::path approval, RunPilotAndApprove(root_, api_));
  auto state = std::make_shared<FakeState>();
  state->failing_call = 3;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  const absl::Status status =
      RunPoseConditionedAnimationBatch(api_, *service,
                                       {.manifest_path = root_ / "experiment.json",
                                        .output_path = root_ / "partial",
                                        .phase = PoseConditionedAnimationPhase::kBatch,
                                        .pilot_approval_path = approval});

  EXPECT_TRUE(absl::IsUnavailable(status));
  ASSERT_OK_AND_ASSIGN(const nlohmann::json manifest, ReadJson(root_ / "partial/manifest.json"));
  EXPECT_EQ(manifest.at("status"), "failed");
  EXPECT_EQ(manifest.at("classification"), "non-candidate-evidence");
  EXPECT_FALSE(manifest.at("animation_candidate").get<bool>());
  EXPECT_EQ(manifest.at("requests").size(), 4);
  EXPECT_FALSE(std::filesystem::exists(root_ / "partial/assembled-source-sheet.png"));
  std::lock_guard lock(state->mutex);
  EXPECT_EQ(state->requests.size(), 4);
}

TEST_F(PoseConditionedAnimationBatchTest, NonuniformPilotOutputIsRetainedButRejected) {
  ASSERT_OK_AND_ASSIGN(const InputKit inputs, WriteInputKit(root_));
  auto state = std::make_shared<FakeState>();
  state->wrong_size_call = 1;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  const absl::Status status =
      RunPoseConditionedAnimationBatch(api_, *service,
                                       {.manifest_path = inputs.manifest,
                                        .output_path = root_ / "wrong-size",
                                        .phase = PoseConditionedAnimationPhase::kPilot});

  EXPECT_TRUE(absl::IsDataLoss(status));
  ASSERT_OK_AND_ASSIGN(const nlohmann::json manifest, ReadJson(root_ / "wrong-size/manifest.json"));
  EXPECT_EQ(manifest.at("status"), "failed");
  EXPECT_EQ(manifest.at("classification"), "non-candidate-evidence");
  ASSERT_EQ(manifest.at("requests").size(), 2);
  EXPECT_EQ(manifest.at("requests")[1].at("raw_outputs")[0].at("width"), kCellWidth + 1);
  EXPECT_TRUE(std::filesystem::exists(root_ / "wrong-size/raw-outputs/frame-06-candidate-0.png"));
}

TEST_F(PoseConditionedAnimationBatchTest, ProviderMismatchRetainsRawFailureEvidence) {
  ASSERT_OK_AND_ASSIGN(const InputKit inputs, WriteInputKit(root_));
  auto state = std::make_shared<FakeState>();
  state->result_provider = "unexpected-fake";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ImageGenerationService> service,
                       ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(state)));

  const absl::Status status =
      RunPoseConditionedAnimationBatch(api_, *service,
                                       {.manifest_path = inputs.manifest,
                                        .output_path = root_ / "provider-mismatch",
                                        .phase = PoseConditionedAnimationPhase::kPilot});

  EXPECT_TRUE(absl::IsDataLoss(status));
  ASSERT_OK_AND_ASSIGN(const nlohmann::json manifest,
                       ReadJson(root_ / "provider-mismatch/manifest.json"));
  EXPECT_EQ(manifest.at("classification"), "non-candidate-evidence");
  ASSERT_EQ(manifest.at("requests").size(), 1);
  EXPECT_EQ(manifest.at("requests")[0].at("provider"), "unexpected-fake");
  ASSERT_EQ(manifest.at("requests")[0].at("raw_outputs").size(), 1);
  EXPECT_TRUE(
      std::filesystem::exists(root_ / "provider-mismatch/raw-outputs/frame-00-candidate-0.png"));
}

TEST_F(PoseConditionedAnimationBatchTest,
       RectangularCompleteBatchRemainsNonCandidateWithoutAspectPreservingNormalization) {
  ASSERT_OK_AND_ASSIGN(const InputKit inputs, WriteInputKit(root_, 12, 18));
  auto pilot_state = std::make_shared<FakeState>();
  pilot_state->output_width = 12;
  pilot_state->output_height = 18;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ImageGenerationService> pilot_service,
      ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(pilot_state)));
  ASSERT_OK(RunPoseConditionedAnimationBatch(api_, *pilot_service,
                                             {.manifest_path = inputs.manifest,
                                              .output_path = root_ / "portrait-pilot",
                                              .phase = PoseConditionedAnimationPhase::kPilot}));
  ASSERT_OK_AND_ASSIGN(const nlohmann::json pilot,
                       ReadJson(root_ / "portrait-pilot/manifest.json"));
  ASSERT_OK(WriteJson(root_ / "portrait-approval.json",
                      {
                          {"schema_version", 1},
                          {"decision", "approved"},
                          {"reviewer", "test-reviewer"},
                          {"reviewed_at_utc", "2026-08-30T12:00:00Z"},
                          {"pilot_manifest", "portrait-pilot/manifest.json"},
                          {"pilot_run_id", pilot.at("run_id")},
                          {"checks",
                           {{"frame_0_fresh_single_render", true},
                            {"frame_0_identity_preserved", true},
                            {"frame_0_pose_obeyed", true},
                            {"frame_6_fresh_single_render", true},
                            {"frame_6_identity_preserved", true},
                            {"frame_6_pose_obeyed", true}}},
                      }));
  auto batch_state = std::make_shared<FakeState>();
  batch_state->output_width = 12;
  batch_state->output_height = 18;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ImageGenerationService> batch_service,
      ImageGenerationService::Create(std::make_unique<ScriptedFakeClient>(batch_state)));

  const absl::Status status =
      RunPoseConditionedAnimationBatch(api_, *batch_service,
                                       {.manifest_path = inputs.manifest,
                                        .output_path = root_ / "portrait-batch",
                                        .phase = PoseConditionedAnimationPhase::kBatch,
                                        .pilot_approval_path = root_ / "portrait-approval.json"});

  EXPECT_TRUE(absl::IsInvalidArgument(status));
  ASSERT_OK_AND_ASSIGN(const nlohmann::json manifest,
                       ReadJson(root_ / "portrait-batch/manifest.json"));
  EXPECT_EQ(manifest.at("requests").size(), 12);
  EXPECT_EQ(manifest.at("classification"), "non-candidate-evidence");
  EXPECT_FALSE(manifest.at("animation_candidate").get<bool>());
  EXPECT_FALSE(manifest.at("processing").at("passed").get<bool>());
  ASSERT_OK_AND_ASSIGN(const RgbaImage assembled,
                       ReadPng((root_ / "portrait-batch/assembled-source-sheet.png").string()));
  EXPECT_EQ(assembled.width, 72);
  EXPECT_EQ(assembled.height, 36);
}

}  // namespace
}  // namespace zebes
