#include "generation/image_generation.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "generation/credential_source.h"
#include "generation/curl_http_transport.h"
#include "generation/http_transport.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {

static_assert(!std::is_copy_constructible_v<SecretString>);
static_assert(!std::is_copy_assignable_v<SecretString>);

struct OperationState {
  int polls = 0;
  int cancellations = 0;
};

RgbaImage OnePixelImage() { return RgbaImage{.width = 1, .height = 1, .pixels = {1, 2, 3, 255}}; }

ImageGenerationReference Reference(
    ImageGenerationReferenceRole role = ImageGenerationReferenceRole::kSubjectIdentity) {
  return ImageGenerationReference{.role = role, .image = OnePixelImage()};
}

ImageGenerationResult ValidGenerationResult() {
  return ImageGenerationResult{
      .provider = "provider",
      .model = "model",
      .submitted_prompt = "a mossy cave boulder",
      .provider_request_id = "request-1",
      .candidates = {{.image = OnePixelImage(), .revised_prompt = std::nullopt}},
  };
}

class FakeImageOperation final : public ImageGenerationOperation {
 public:
  FakeImageOperation(std::shared_ptr<OperationState> state, ImageGenerationResult result)
      : state_(std::move(state)), result_(std::move(result)) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    ++state_->polls;
    if (state_->polls == 1) return std::nullopt;
    return std::optional<ImageGenerationResult>(std::move(result_));
  }

  void Cancel() noexcept override { ++state_->cancellations; }

 private:
  std::shared_ptr<OperationState> state_;
  ImageGenerationResult result_;
};

class FakeHttpOperation final : public HttpOperation {
 public:
  FakeHttpOperation(std::shared_ptr<OperationState> state, HttpResponse response)
      : state_(std::move(state)), response_(std::move(response)) {}

  absl::StatusOr<std::optional<HttpResponse>> Poll() override {
    ++state_->polls;
    return std::optional<HttpResponse>(std::move(response_));
  }

  void Cancel() noexcept override { ++state_->cancellations; }

 private:
  std::shared_ptr<OperationState> state_;
  HttpResponse response_;
};

class FakeImageClient final : public ImageGenerationClient {
 public:
  FakeImageClient(std::shared_ptr<OperationState> state, ImageGenerationResult result)
      : state_(std::move(state)), result_(std::move(result)) {}

  ImageGenerationCapabilities Capabilities() const override {
    return ImageGenerationCapabilities{};
  }

  int starts() const { return starts_; }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec) override {
    ++starts_;
    return ImageGenerationRequest::Create(
        std::make_unique<FakeImageOperation>(state_, std::move(result_)));
  }

 private:
  std::shared_ptr<OperationState> state_;
  ImageGenerationResult result_;
  int starts_ = 0;
};

class FakeHttpTransport final : public HttpTransport {
 public:
  FakeHttpTransport(std::shared_ptr<OperationState> state, HttpResponse response)
      : state_(std::move(state)), response_(std::move(response)) {}

  int starts() const { return starts_; }

 protected:
  absl::StatusOr<HttpRequestHandle> StartValidated(HttpRequest) override {
    ++starts_;
    return HttpRequestHandle::Create(
        std::make_unique<FakeHttpOperation>(state_, std::move(response_)));
  }

 private:
  std::shared_ptr<OperationState> state_;
  HttpResponse response_;
  int starts_ = 0;
};

TEST(ImageGenerationContractTest, ValidatesRequestedProviderCapabilities) {
  const ImageGenerationCapabilities capabilities{
      .maximum_candidates = 4,
      .supports_negative_prompt = true,
      .supports_transparency = true,
      .maximum_reference_images = 2,
      .maximum_reference_pixels = 2,
  };
  const ImageGenerationSpec spec{
      .prompt = "an isolated cave boulder",
      .instructions = "Use a transparent background.",
      .negative_prompt = "text",
      .requested_candidates = 2,
      .target_aspect = {.width = 4, .height = 3},
      .transparency = ImageTransparencyPreference::kPreferTransparent,
      .references = {Reference(ImageGenerationReferenceRole::kSubjectIdentity),
                     Reference(ImageGenerationReferenceRole::kPose)},
  };

  EXPECT_TRUE(ValidateImageGenerationSpec(spec, capabilities).ok());
}

TEST(ImageGenerationContractTest, RejectsUnsupportedRequestedCapabilities) {
  const ImageGenerationCapabilities capabilities;
  ImageGenerationSpec spec{.prompt = "boulder"};
  spec.negative_prompt = "text";
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);
  spec.negative_prompt.reset();
  spec.transparency = ImageTransparencyPreference::kPreferTransparent;
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);
  spec.transparency = ImageTransparencyPreference::kNoPreference;
  spec.references = {Reference()};
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ImageGenerationContractTest, RejectsInvalidCoreInputs) {
  ImageGenerationCapabilities capabilities{
      .maximum_candidates = 2,
      .supports_negative_prompt = true,
      .maximum_reference_images = 2,
      .maximum_reference_pixels = 2,
  };
  ImageGenerationSpec spec{.prompt = "boulder"};

  spec.instructions = "";
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);
  spec.instructions.reset();
  spec.requested_candidates = 0;
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);
  spec.requested_candidates = 1;
  spec.target_aspect.width = 0;
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);
  spec.target_aspect.width = 1;
  spec.negative_prompt = "";
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);
  spec.negative_prompt.reset();
  spec.references = {
      ImageGenerationReference{.role = ImageGenerationReferenceRole::kPose, .image = RgbaImage{}}};
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);

  capabilities.maximum_candidates = 0;
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(ImageGenerationContractTest, RejectsInvalidReferenceRolesOrderCountAndPixels) {
  ImageGenerationCapabilities capabilities{
      .maximum_candidates = 1,
      .maximum_reference_images = 2,
      .maximum_reference_pixels = 2,
  };
  ImageGenerationSpec spec{.prompt = "boulder"};

  spec.references = {Reference(ImageGenerationReferenceRole::kSubjectIdentity),
                     Reference(ImageGenerationReferenceRole::kPose),
                     Reference(ImageGenerationReferenceRole::kPose)};
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);

  spec.references = {
      ImageGenerationReference{
          .role = ImageGenerationReferenceRole::kPose,
          .image = RgbaImage{.width = 2, .height = 1, .pixels = {1, 2, 3, 255, 4, 5, 6, 255}}},
      Reference(ImageGenerationReferenceRole::kSubjectIdentity),
  };
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);

  spec.references = {Reference(ImageGenerationReferenceRole::kSubjectIdentity),
                     Reference(ImageGenerationReferenceRole::kEditSource)};
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);

  spec.references = {Reference(static_cast<ImageGenerationReferenceRole>(99))};
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kInvalidArgument);

  capabilities.maximum_reference_images = 0;
  EXPECT_EQ(ValidateImageGenerationSpec(spec, capabilities).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(ImageGenerationContractTest, ParsesStableReferenceRoleNames) {
  EXPECT_EQ(ImageGenerationReferenceRoleName(ImageGenerationReferenceRole::kEditSource),
            "edit-source");
  EXPECT_EQ(ImageGenerationReferenceRoleName(ImageGenerationReferenceRole::kSubjectIdentity),
            "subject-identity");
  EXPECT_EQ(ImageGenerationReferenceRoleName(ImageGenerationReferenceRole::kPose), "pose");
  ASSERT_OK_AND_ASSIGN(const ImageGenerationReferenceRole role,
                       ParseImageGenerationReferenceRole("subject-identity"));
  EXPECT_EQ(role, ImageGenerationReferenceRole::kSubjectIdentity);
  EXPECT_EQ(ParseImageGenerationReferenceRole("style").status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ImageGenerationContractTest, ComposesOptionalInstructionsWithoutChangingTheSubjectPrompt) {
  ImageGenerationSpec spec{
      .prompt = "a mossy boulder",
      .instructions = "Create one isolated prop.",
  };

  EXPECT_EQ(ComposeImageGenerationPrompt(spec),
            "Create one isolated prop.\n\nSubject request:\na mossy boulder");
  EXPECT_EQ(spec.prompt, "a mossy boulder");

  spec.instructions.reset();
  EXPECT_EQ(ComposeImageGenerationPrompt(spec), "a mossy boulder");
}

TEST(ImageGenerationContractTest, ComposesOneSharedIndexedReferenceLegend) {
  const ImageGenerationSpec spec{
      .prompt = "one right-facing runner",
      .instructions = "Create one isolated game character.",
      .references = {Reference(ImageGenerationReferenceRole::kSubjectIdentity),
                     Reference(ImageGenerationReferenceRole::kPose)},
  };

  const std::string turn =
      "Reference inputs:\n"
      "Reference 1 (subject-identity): preserve this subject's identity, proportions, design, "
      "palette, and identifying landmarks; do not copy the reference layout or pose.\n"
      "Reference 2 (pose): use only its pose, facing, limb geometry, and ground contact; do not "
      "copy its appearance, line style, or background.\n\n"
      "Subject request:\n"
      "one right-facing runner";
  EXPECT_EQ(ComposeImageGenerationTurnPrompt(spec), turn);
  EXPECT_EQ(ComposeImageGenerationPrompt(spec), "Create one isolated game character.\n\n" + turn);
}

TEST(ImageGenerationContractTest, RejectsIncompleteStableProvenanceAndCandidates) {
  ImageGenerationResult result = ValidGenerationResult();
  result.provider.clear();
  EXPECT_EQ(ValidateImageGenerationResult(result).code(), absl::StatusCode::kInvalidArgument);
  result = ValidGenerationResult();
  result.provider_request_id = "";
  EXPECT_EQ(ValidateImageGenerationResult(result).code(), absl::StatusCode::kInvalidArgument);
  result = ValidGenerationResult();
  result.candidates.front().image = RgbaImage{};
  EXPECT_EQ(ValidateImageGenerationResult(result).code(), absl::StatusCode::kInvalidArgument);
  result = ValidGenerationResult();
  result.candidates.front().revised_prompt = "";
  EXPECT_EQ(ValidateImageGenerationResult(result).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ImageGenerationContractTest, ClientRejectsAnInvalidSpecBeforeCallingTheAdapter) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  FakeImageClient client(state, ValidGenerationResult());

  EXPECT_EQ(client.Start(ImageGenerationSpec{}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(client.starts(), 0);
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       client.Start(ImageGenerationSpec{.prompt = "boulder"}));
  EXPECT_EQ(client.starts(), 1);
}

TEST(ImageGenerationContractTest, ClientRejectsMoreCandidatesThanRequested) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  ImageGenerationResult result = ValidGenerationResult();
  result.candidates.push_back(
      ImageGenerationCandidate{.image = OnePixelImage(), .revised_prompt = std::nullopt});
  FakeImageClient client(state, std::move(result));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       client.Start(ImageGenerationSpec{.prompt = "boulder"}));

  ASSERT_OK_AND_ASSIGN(std::optional<ImageGenerationResult> pending, request.Poll());
  EXPECT_FALSE(pending.has_value());
  EXPECT_EQ(request.Poll().status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(state->cancellations, 1);
}

TEST(ImageGenerationContractTest, RequestPollsWithoutBlockingAndCompletesOnce) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       ImageGenerationRequest::Create(
                           std::make_unique<FakeImageOperation>(state, ValidGenerationResult())));

  ASSERT_OK_AND_ASSIGN(std::optional<ImageGenerationResult> pending, request.Poll());
  EXPECT_FALSE(pending.has_value());
  ASSERT_OK_AND_ASSIGN(std::optional<ImageGenerationResult> complete, request.Poll());
  ASSERT_TRUE(complete.has_value());
  EXPECT_EQ(complete->candidates.size(), 1);
  EXPECT_FALSE(request.active());
  EXPECT_EQ(request.Poll().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(state->cancellations, 0);
}

TEST(ImageGenerationContractTest, UnfinishedRequestCancelsOnDestruction) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  {
    ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                         ImageGenerationRequest::Create(
                             std::make_unique<FakeImageOperation>(state, ValidGenerationResult())));
    EXPECT_TRUE(request.active());
  }
  EXPECT_EQ(state->cancellations, 1);
}

TEST(ImageGenerationContractTest, InvalidProviderResultFailsAsDataLoss) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  ImageGenerationResult invalid = ValidGenerationResult();
  invalid.candidates.clear();
  ASSERT_OK_AND_ASSIGN(
      ImageGenerationRequest request,
      ImageGenerationRequest::Create(std::make_unique<FakeImageOperation>(state, invalid)));

  ASSERT_OK_AND_ASSIGN(std::optional<ImageGenerationResult> pending, request.Poll());
  EXPECT_FALSE(pending.has_value());
  EXPECT_EQ(request.Poll().status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(state->cancellations, 1);
}

TEST(CredentialSourceTest, LoadsASecretThroughTheEnvironmentBoundary) {
  const EnvironmentCredentialSource source([](absl::string_view name) {
    EXPECT_EQ(name, "OPENAI_API_KEY");
    return std::optional<std::string>("super-secret");
  });

  ASSERT_OK_AND_ASSIGN(SecretString secret, source.Load("OPENAI_API_KEY"));
  EXPECT_EQ(secret.value(), "super-secret");
}

TEST(CredentialSourceTest, RejectsAnEmptySecretAndAnUnconfiguredReader) {
  EXPECT_EQ(SecretString::Create("").status().code(), absl::StatusCode::kInvalidArgument);
  const EnvironmentCredentialSource source(EnvironmentCredentialSource::Reader{});
  EXPECT_EQ(source.Load("OPENAI_API_KEY").status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(CredentialSourceTest, MissingAndEmptyCredentialsAreUnauthenticated) {
  const EnvironmentCredentialSource missing(
      [](absl::string_view) { return std::optional<std::string>(); });
  const EnvironmentCredentialSource empty(
      [](absl::string_view) { return std::optional<std::string>(""); });

  EXPECT_EQ(missing.Load("OPENAI_API_KEY").status().code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(empty.Load("OPENAI_API_KEY").status().code(), absl::StatusCode::kUnauthenticated);
}

TEST(CredentialSourceTest, RejectsAnInvalidEnvironmentVariableNameBeforeReading) {
  bool read = false;
  const EnvironmentCredentialSource source([&read](absl::string_view) {
    read = true;
    return std::optional<std::string>("secret");
  });

  EXPECT_EQ(source.Load("OPENAI_API_KEY=secret").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(read);
}

TEST(HttpTransportContractTest, RequiresBoundedHttpsRequests) {
  ASSERT_OK_AND_ASSIGN(SecretString secret, SecretString::Create("secret"));
  HttpRequest request{
      .url = "https://api.example.test/v1/images",
      .headers = {{.name = "Content-Type", .value = "application/json"}},
      .sensitive_headers = {},
      .body = {'{', '}'},
  };
  request.sensitive_headers.push_back(
      HttpSensitiveHeader{.name = "Authorization", .value = std::move(secret)});
  EXPECT_TRUE(ValidateHttpRequest(request).ok());

  request.url = "http://api.example.test/v1/images";
  EXPECT_EQ(ValidateHttpRequest(request).code(), absl::StatusCode::kInvalidArgument);
}

TEST(HttpTransportContractTest, RejectsInvalidTimeoutsLimitsAndHeaders) {
  HttpRequest request{.url = "https://api.example.test/v1/images"};
  request.method = static_cast<HttpMethod>(255);
  EXPECT_EQ(ValidateHttpRequest(request).code(), absl::StatusCode::kInvalidArgument);
  request.method = HttpMethod::kPost;
  request.connect_timeout = std::chrono::milliseconds::zero();
  EXPECT_EQ(ValidateHttpRequest(request).code(), absl::StatusCode::kInvalidArgument);
  request.connect_timeout = std::chrono::milliseconds(10);
  request.total_timeout = std::chrono::milliseconds(5);
  EXPECT_EQ(ValidateHttpRequest(request).code(), absl::StatusCode::kInvalidArgument);
  request.total_timeout = std::chrono::milliseconds(10);
  request.maximum_response_bytes = 0;
  EXPECT_EQ(ValidateHttpRequest(request).code(), absl::StatusCode::kInvalidArgument);
  request.maximum_response_bytes = 1;
  request.headers.push_back(HttpHeader{.name = "X-Test", .value = "unsafe\r\nvalue"});
  EXPECT_EQ(ValidateHttpRequest(request).code(), absl::StatusCode::kInvalidArgument);

  EXPECT_EQ(ValidateHttpResponse(HttpResponse{.status_code = 0}).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ValidateHttpResponse(HttpResponse{
                                     .status_code = 200,
                                     .headers = {{.name = "X-Test", .value = "unsafe\nvalue"}},
                                 })
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(HttpTransportContractTest, HandleCancelsOnlyWhileActive) {
  const std::shared_ptr<OperationState> completed_state = std::make_shared<OperationState>();
  ASSERT_OK_AND_ASSIGN(HttpRequestHandle complete,
                       HttpRequestHandle::Create(std::make_unique<FakeHttpOperation>(
                           completed_state, HttpResponse{.status_code = 200})));
  ASSERT_OK_AND_ASSIGN(std::optional<HttpResponse> response, complete.Poll());
  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->status_code, 200);
  EXPECT_EQ(completed_state->cancellations, 0);

  const std::shared_ptr<OperationState> cancelled_state = std::make_shared<OperationState>();
  {
    ASSERT_OK_AND_ASSIGN(HttpRequestHandle cancelled,
                         HttpRequestHandle::Create(std::make_unique<FakeHttpOperation>(
                             cancelled_state, HttpResponse{.status_code = 200})));
  }
  EXPECT_EQ(cancelled_state->cancellations, 1);
}

TEST(HttpTransportContractTest, TransportRejectsAnInvalidRequestBeforeStartingIO) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  FakeHttpTransport transport(state, HttpResponse{.status_code = 200});

  EXPECT_EQ(transport.Start(HttpRequest{.url = "http://example.test"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(transport.starts(), 0);
  ASSERT_OK_AND_ASSIGN(HttpRequestHandle request,
                       transport.Start(HttpRequest{.url = "https://example.test"}));
  EXPECT_EQ(transport.starts(), 1);
}

TEST(HttpTransportContractTest, HandleBackstopsTheResponseByteLimit) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  FakeHttpTransport transport(state, HttpResponse{.status_code = 200, .body = {1, 2}});
  ASSERT_OK_AND_ASSIGN(HttpRequestHandle request, transport.Start(HttpRequest{
                                                      .url = "https://example.test",
                                                      .maximum_response_bytes = 1,
                                                  }));

  EXPECT_EQ(request.Poll().status().code(), absl::StatusCode::kResourceExhausted);
  EXPECT_EQ(state->cancellations, 1);
}

TEST(CurlHttpTransportTest, ConfiguresAndCancelsARequestWithoutStartingNetworkIO) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CurlHttpTransport> transport, CurlHttpTransport::Create());
  ASSERT_OK_AND_ASSIGN(SecretString secret, SecretString::Create("secret"));
  HttpRequest spec{
      .url = "https://api.example.test/v1/images",
      .headers = {{.name = "Content-Type", .value = "application/json"}},
      .body = {'{', '}'},
  };
  spec.sensitive_headers.push_back(
      HttpSensitiveHeader{.name = "Authorization", .value = std::move(secret)});

  ASSERT_OK_AND_ASSIGN(HttpRequestHandle request, transport->Start(std::move(spec)));
  EXPECT_TRUE(request.active());
  request.Cancel();
  EXPECT_FALSE(request.active());
}

// The bound a caller sleeping between polls depends on. curl's own answer is
// never handed through unclamped: a negative timeout means "wait on the
// sockets", which this transport does not expose, so sleeping on it literally
// would stall the transfer until its total timeout.
//
// A newly added handle reports zero, because curl wants the first
// curl_multi_perform immediately and has no timer until it runs. Sleeping
// before starting the transfer would be the wrong answer, so zero is right
// here and the caller polls straight through to Poll.
//
// The negative-timeout branch needs an established socket and so has no
// headless coverage; the opt-in live integration test is what reaches it.
TEST(CurlHttpTransportTest, BoundsThePollDelayOfALiveRequest) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CurlHttpTransport> transport, CurlHttpTransport::Create());
  ASSERT_OK_AND_ASSIGN(HttpRequestHandle request, transport->Start(HttpRequest{
                                                      .url = "https://api.example.test/v1/images",
                                                      .body = {'{', '}'},
                                                  }));

  const absl::Duration delay = request.SuggestedPollDelay();
  EXPECT_GE(delay, absl::ZeroDuration());
  EXPECT_LE(delay, CurlHttpTransport::kPollCap);

  // A finished request must not be slept on: the caller has to poll to find out
  // it is no longer active.
  request.Cancel();
  EXPECT_EQ(request.SuggestedPollDelay(), absl::ZeroDuration());
}

TEST(HttpTransportContractTest, HandleReportsNoPollDelayOnceTheRequestCompletes) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  ASSERT_OK_AND_ASSIGN(HttpRequestHandle request,
                       HttpRequestHandle::Create(std::make_unique<FakeHttpOperation>(
                           state, HttpResponse{.status_code = 200})));

  EXPECT_GT(request.SuggestedPollDelay(), absl::ZeroDuration());
  ASSERT_OK(request.Poll().status());
  EXPECT_EQ(request.SuggestedPollDelay(), absl::ZeroDuration());
}

TEST(ImageGenerationContractTest, RequestReportsNoPollDelayOnceItCompletes) {
  const std::shared_ptr<OperationState> state = std::make_shared<OperationState>();
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       ImageGenerationRequest::Create(
                           std::make_unique<FakeImageOperation>(state, ValidGenerationResult())));

  EXPECT_GT(request.SuggestedPollDelay(), absl::ZeroDuration());
  ASSERT_OK(request.Poll().status());
  ASSERT_OK(request.Poll().status());
  EXPECT_FALSE(request.active());
  EXPECT_EQ(request.SuggestedPollDelay(), absl::ZeroDuration());
}

}  // namespace
}  // namespace zebes
