// Opt-in integration test for the real OpenAI image adapter.
//
// This binary is deliberately not registered with CTest and does not land in
// bin/tests, so neither `ctest` nor `scripts/test.sh --affected-target` can
// select it. It runs only when someone builds and invokes it directly:
//
//   set -a; source secrets.env; set +a
//   cmake --build --preset dev --target openai_image_client_live_test
//   build/dev/bin/tools/openai_image_client_live_test
//
// secrets.env is the gitignored copy of secrets.env.example; see README.md.
//
// Generating spends real money and depends on a third-party service, so this
// can never gate a merge. Its job is the one contract no fake reaches: the
// whole real stack completing a generation against a live TLS endpoint.
//
// It is the happy path and nothing else. Cancellation, error mapping, and
// malformed responses are all deterministic behavior that the fake-driven
// tests in tests/editor already own; reproducing them here would buy races
// and provider dependence for coverage that exists.
//
// A missing credential is a failure, not a skip. Nothing runs this binary by
// accident, so an unset key means the operator meant to test and could not.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "generation/credential_source.h"
#include "generation/curl_http_transport.h"
#include "generation/image_generation.h"
#include "generation/openai_image_client.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

// gpt-image-2 at the cheapest tier still takes tens of seconds. This bounds a
// hung transfer without racing a slow-but-working one.
constexpr absl::Duration kGenerationBudget = absl::Minutes(3);

struct LiveGeneration {
  ImageGenerationResult result;

  // True once SuggestedPollDelay returned the transport's cap. Reaching the cap
  // means curl offered no timer of its own, which is the negative-timeout
  // branch this binary exists to exercise. It is not a perfect discriminator:
  // a curl timer of exactly kPollCap would be indistinguishable. Over a
  // multi-second wait on an established socket, the branch is what produces it.
  bool reached_poll_cap = false;
};

absl::StatusOr<LiveGeneration> RunToCompletion(ImageGenerationRequest& request) {
  const absl::Time deadline = absl::Now() + kGenerationBudget;
  LiveGeneration generation;
  while (absl::Now() < deadline) {
    const absl::Duration delay = request.SuggestedPollDelay();
    if (delay == CurlHttpTransport::kPollCap) generation.reached_poll_cap = true;
    absl::SleepFor(delay);

    ASSIGN_OR_RETURN(std::optional<ImageGenerationResult> result, request.Poll());
    if (!result.has_value()) continue;
    generation.result = *std::move(result);
    return generation;
  }
  return absl::DeadlineExceededError("live generation did not finish within its budget");
}

class OpenAiImageClientLiveTest : public ::testing::Test {
 protected:
  // Every field except quality matches production. The test asserts transport
  // and adapter behavior, which does not vary with render quality, so it runs
  // at the cheapest tier the model accepts rather than the authoring default.
  static OpenAiImageConfig TestConfig() {
    OpenAiImageConfig config;
    config.quality = "low";
    return config;
  }

  static ImageGenerationSpec TestSpec() {
    return ImageGenerationSpec{
        .prompt = "a small grey boulder on a plain background, pixel art",
        .requested_candidates = 1,
    };
  }

  void SetUp() override {
    const OpenAiImageConfig config = TestConfig();
    ASSERT_OK_AND_ASSIGN(SecretString key, credentials_.Load(config.credential_reference));
    ASSERT_FALSE(key.value().empty())
        << config.credential_reference << " is empty; export a real key to run this binary";

    ASSERT_OK_AND_ASSIGN(transport_, CurlHttpTransport::Create());
    ASSERT_OK_AND_ASSIGN(client_, OpenAiImageClient::Create(*transport_, credentials_, config));
  }

  EnvironmentCredentialSource credentials_;
  std::unique_ptr<CurlHttpTransport> transport_;
  std::unique_ptr<OpenAiImageClient> client_;
};

// A free preflight: run it alone with --gtest_filter to confirm the key, the
// libcurl build, and the adapter compose before spending anything on the
// generation below.
TEST_F(OpenAiImageClientLiveTest, ComposesTheRealStackWithoutARequest) {
  const ImageGenerationCapabilities capabilities = client_->Capabilities();
  EXPECT_GE(capabilities.maximum_candidates, 1);
}

TEST_F(OpenAiImageClientLiveTest, GeneratesOneCandidateAndBoundsItsPollDelay) {
  const ImageGenerationSpec spec = TestSpec();
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, client_->Start(spec));
  ASSERT_OK_AND_ASSIGN(const LiveGeneration generation, RunToCompletion(request));

  EXPECT_TRUE(generation.reached_poll_cap)
      << "no poll reached the transport cap, so the negative-timeout branch went unexercised";

  ASSERT_EQ(generation.result.candidates.size(), 1);
  const RgbaImage& image = generation.result.candidates.front().image;
  EXPECT_TRUE(image.IsValid());
  // Matches OpenAiImageConfig::size, which the adapter sends verbatim.
  EXPECT_EQ(image.width, 1024);
  EXPECT_EQ(image.height, 1024);

  EXPECT_EQ(generation.result.provider, "openai");
  EXPECT_EQ(generation.result.model, TestConfig().model);
  EXPECT_EQ(generation.result.submitted_prompt, spec.prompt);

  // A finished request must not be slept on; the caller polls to learn it is
  // no longer active.
  EXPECT_FALSE(request.active());
  EXPECT_EQ(request.SuggestedPollDelay(), absl::ZeroDuration());
}

}  // namespace
}  // namespace zebes
