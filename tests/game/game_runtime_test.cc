#include "game/game_runtime.h"

#include <cstddef>
#include <memory>

#include "absl/status/status.h"
#include "common/config.h"
#include "engine/input_types.h"
#include "game/game_renderer.h"
#include "game/game_scene.h"
#include "game/simulation_pacer.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "platform/headless/headless_texture_store.h"

namespace zebes {
namespace {

constexpr char kAssetsRoot[] = ZEBES_TEST_ASSETS_DIR;

class QuitAfterOneFrameInput final : public InputSource {
 public:
  InputSnapshot Poll() override { return {.quit_requested = poll_count_++ > 0}; }

 private:
  size_t poll_count_ = 0;
};

class RecordingGameRenderer final : public GameRenderer {
 public:
  absl::Status Render(const GameSceneFrame& frame) const override {
    ++frame_count_;
    last_frame_ = frame;
    return absl::OkStatus();
  }

  size_t frame_count() const { return frame_count_; }
  const GameSceneFrame& last_frame() const { return last_frame_; }

 private:
  mutable size_t frame_count_ = 0;
  mutable GameSceneFrame last_frame_;
};

TEST(GameRuntimeTest, BootsAndRunsAFrameThroughPlatformNeutralDependencies) {
  EngineConfig config;
  const GameViewSize game_view = config.game_view;
  QuitAfterOneFrameInput input;
  HeadlessTextureStore textures;
  RecordingGameRenderer renderer;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<GameRuntime> runtime,
                       GameRuntime::Create({
                           .config = std::move(config),
                           .asset_root = kAssetsRoot,
                           .input_source = &input,
                           .texture_resources = &textures,
                           .renderer = &renderer,
                           .pacing_mode = SimulationPacingMode::kUnpaced,
                       }));

  EXPECT_OK(runtime->Run());

  ASSERT_EQ(renderer.frame_count(), 1u);
  EXPECT_EQ(renderer.last_frame().camera.viewport_width, game_view.width);
  EXPECT_EQ(renderer.last_frame().camera.viewport_height, game_view.height);
  EXPECT_FALSE(renderer.last_frame().world_layers.empty());
}

TEST(GameRuntimeTest, RejectsMissingPlatformDependenciesBeforeLoadingAssets) {
  EngineConfig config;
  const absl::Status status = GameRuntime::Create({
                                                      .config = std::move(config),
                                                      .asset_root = kAssetsRoot,
                                                  })
                                  .status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "Game runtime input source is null");
}

}  // namespace
}  // namespace zebes
