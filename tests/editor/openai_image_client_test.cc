#include "editor/image_generation/openai_image_client.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "editor/image_generation/credential_source.h"
#include "editor/image_generation/http_transport.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

std::vector<uint8_t> ToBytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

// A real PNG, base64 encoded, as a candidate body would carry it.
std::string EncodedCandidate(int width, int height) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "openai_candidate.png").string();
  std::filesystem::remove(path);
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  for (size_t index = 0; index < pixels.size(); index += 4) {
    pixels[index + 0] = 40;
    pixels[index + 1] = 90;
    pixels[index + 2] = 60;
    pixels[index + 3] = 255;
  }
  EXPECT_TRUE(WritePng(path, width, height, pixels).ok());
  std::ifstream file(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  return absl::Base64Escape(bytes);
}

class FakeOperation final : public HttpOperation {
 public:
  explicit FakeOperation(HttpResponse response) : response_(std::move(response)) {}

  absl::StatusOr<std::optional<HttpResponse>> Poll() override {
    return std::optional<HttpResponse>(std::move(response_));
  }

  void Cancel() noexcept override {}

 private:
  HttpResponse response_;
};

// Records the request so the tests can assert what actually went on the wire.
class FakeTransport final : public HttpTransport {
 public:
  explicit FakeTransport(HttpResponse response) : response_(std::move(response)) {}

  const HttpRequest& last_request() const { return last_request_; }

 protected:
  absl::StatusOr<HttpRequestHandle> StartValidated(HttpRequest request) override {
    last_request_.method = request.method;
    last_request_.url = request.url;
    last_request_.headers = request.headers;
    last_request_.body = request.body;
    for (const HttpSensitiveHeader& header : request.sensitive_headers) {
      sensitive_names_.push_back(header.name);
      sensitive_values_.push_back(std::string(header.value.value()));
    }
    return HttpRequestHandle::Create(std::make_unique<FakeOperation>(response_));
  }

 public:
  std::vector<std::string> sensitive_names_;
  std::vector<std::string> sensitive_values_;

 private:
  HttpResponse response_;
  HttpRequest last_request_;
};

class FakeCredentials final : public CredentialSource {
 public:
  explicit FakeCredentials(std::string value) : value_(std::move(value)) {}

  absl::StatusOr<SecretString> Load(absl::string_view reference) const override {
    if (value_.empty()) {
      return absl::UnauthenticatedError(absl::StrCat("no credential for ", reference));
    }
    return SecretString::Create(value_);
  }

 private:
  std::string value_;
};

HttpResponse JsonResponse(int status_code, const std::string& body) {
  return HttpResponse{
      .status_code = status_code,
      .headers = {{.name = "Content-Type", .value = "application/json"}},
      .body = ToBytes(body),
  };
}

std::string SuccessBody(int candidates) {
  nlohmann::json data = nlohmann::json::array();
  for (int index = 0; index < candidates; ++index) {
    data.push_back({{"b64_json", EncodedCandidate(4, 4)}});
  }
  return nlohmann::json{{"data", data}}.dump();
}

ImageGenerationSpec SpecFor(const std::string& prompt, int candidates = 1) {
  return ImageGenerationSpec{
      .prompt = prompt,
      .requested_candidates = candidates,
      .target_aspect = {.width = 1, .height = 1},
  };
}

struct Fixture {
  std::unique_ptr<FakeTransport> transport;
  std::unique_ptr<FakeCredentials> credentials;
  std::unique_ptr<OpenAiImageClient> client;
};

absl::StatusOr<Fixture> MakeClient(HttpResponse response, std::string credential = "sk-test") {
  Fixture fixture;
  fixture.transport = std::make_unique<FakeTransport>(std::move(response));
  fixture.credentials = std::make_unique<FakeCredentials>(std::move(credential));
  ASSIGN_OR_RETURN(
      fixture.client,
      OpenAiImageClient::Create(*fixture.transport, *fixture.credentials, OpenAiImageConfig{}));
  return fixture;
}

TEST(OpenAiImageClientTest, ReportsCapabilitiesThatMatchTheModel) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, SuccessBody(1))));

  const ImageGenerationCapabilities capabilities = fixture.client->Capabilities();

  EXPECT_EQ(capabilities.maximum_candidates, 10);
  // gpt-image-2 rejects background=transparent; claiming otherwise would let a
  // spec through that the provider then refuses.
  EXPECT_FALSE(capabilities.supports_transparency);
  EXPECT_FALSE(capabilities.supports_negative_prompt);
  EXPECT_FALSE(capabilities.supports_reference_image);
}

TEST(OpenAiImageClientTest, RefusesTransparencyBeforeReachingTheProvider) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, SuccessBody(1))));
  ImageGenerationSpec spec = SpecFor("a mossy boulder");
  spec.transparency = ImageTransparencyPreference::kPreferTransparent;

  const absl::Status status = fixture.client->Start(std::move(spec)).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(fixture.transport->last_request().url.empty()) << "no request should be sent";
}

TEST(OpenAiImageClientTest, SendsTheCredentialOnlyAsASensitiveHeader) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, SuccessBody(1))));

  ASSERT_OK(fixture.client->Start(SpecFor("a mossy boulder")).status());

  const HttpRequest& request = fixture.transport->last_request();
  ASSERT_EQ(fixture.transport->sensitive_names_.size(), 1);
  EXPECT_EQ(fixture.transport->sensitive_names_[0], "Authorization");
  EXPECT_EQ(fixture.transport->sensitive_values_[0], "Bearer sk-test");
  for (const HttpHeader& header : request.headers) {
    EXPECT_EQ(header.value.find("sk-test"), std::string::npos) << "secret in a plain header";
  }
  const std::string body(request.body.begin(), request.body.end());
  EXPECT_EQ(body.find("sk-test"), std::string::npos) << "secret in the request body";
}

TEST(OpenAiImageClientTest, BuildsTheGenerationsRequest) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, SuccessBody(1))));

  ASSERT_OK(fixture.client->Start(SpecFor("a mossy boulder", 3)).status());

  const HttpRequest& request = fixture.transport->last_request();
  EXPECT_EQ(request.url, "https://api.openai.com/v1/images/generations");
  const std::string body(request.body.begin(), request.body.end());
  const nlohmann::json payload = nlohmann::json::parse(body);
  EXPECT_EQ(payload.at("model"), "gpt-image-2");
  EXPECT_EQ(payload.at("prompt"), "a mossy boulder");
  EXPECT_EQ(payload.at("n"), 3);
  EXPECT_EQ(payload.at("output_format"), "png");
}

TEST(OpenAiImageClientTest, DecodesCandidatesWithStableProvenance) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, SuccessBody(2))));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       fixture.client->Start(SpecFor("a mossy boulder", 2)));

  ASSERT_OK_AND_ASSIGN(std::optional<ImageGenerationResult> result, request.Poll());

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->provider, "openai");
  EXPECT_EQ(result->model, "gpt-image-2");
  EXPECT_EQ(result->submitted_prompt, "a mossy boulder");
  ASSERT_EQ(result->candidates.size(), 2);
  EXPECT_TRUE(result->candidates[0].image.IsValid());
  EXPECT_EQ(result->candidates[0].image.width, 4);
}

TEST(OpenAiImageClientTest, CarriesARevisedPromptWhenTheProviderSendsOne) {
  const nlohmann::json body{
      {"data", nlohmann::json::array({{{"b64_json", EncodedCandidate(4, 4)},
                                       {"revised_prompt", "a mossy granite boulder"}}})}};
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, body.dump())));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       fixture.client->Start(SpecFor("a mossy boulder")));

  ASSERT_OK_AND_ASSIGN(std::optional<ImageGenerationResult> result, request.Poll());

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->candidates.size(), 1);
  ASSERT_TRUE(result->candidates[0].revised_prompt.has_value());
  EXPECT_EQ(*result->candidates[0].revised_prompt, "a mossy granite boulder");
}

// Each provider failure has to arrive as a distinct code, because the editor
// tells the user something different for a bad key than for a rate limit.
TEST(OpenAiImageClientTest, MapsProviderStatusCodesToDistinctErrors) {
  struct Case {
    int status_code;
    absl::StatusCode expected;
  };
  const std::vector<Case> cases{
      {400, absl::StatusCode::kInvalidArgument},   {401, absl::StatusCode::kUnauthenticated},
      {403, absl::StatusCode::kPermissionDenied},  {404, absl::StatusCode::kNotFound},
      {429, absl::StatusCode::kResourceExhausted}, {418, absl::StatusCode::kInvalidArgument},
      {500, absl::StatusCode::kUnavailable},       {503, absl::StatusCode::kUnavailable},
  };

  for (const Case& test_case : cases) {
    const std::string body = nlohmann::json{{"error", {{"message", "provider said no"}}}}.dump();
    ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(test_case.status_code, body)));
    ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                         fixture.client->Start(SpecFor("a mossy boulder")));

    const absl::Status status = request.Poll().status();

    EXPECT_EQ(status.code(), test_case.expected) << "HTTP " << test_case.status_code;
    EXPECT_NE(status.message().find("provider said no"), std::string::npos)
        << "HTTP " << test_case.status_code;
  }
}

TEST(OpenAiImageClientTest, ReportsAFailureWithNoErrorBody) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(500, "not json at all")));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       fixture.client->Start(SpecFor("a mossy boulder")));

  const absl::Status status = request.Poll().status();

  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
}

TEST(OpenAiImageClientTest, ReportsMalformedSuccessBodiesAsDataLoss) {
  const std::vector<std::string> bodies{
      "not json at all",
      R"({"data": "not an array"})",
      R"({"data": [{"no_image": true}]})",
      R"({"data": [{"b64_json": "!!!not base64!!!"}]})",
      R"({"data": [{"b64_json": "aGVsbG8="}]})",  // valid base64, not an image
  };

  for (const std::string& body : bodies) {
    ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, body)));
    ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                         fixture.client->Start(SpecFor("a mossy boulder")));

    EXPECT_EQ(request.Poll().status().code(), absl::StatusCode::kDataLoss) << body;
  }
}

TEST(OpenAiImageClientTest, ReportsAnEmptyCandidateSetAsNotFound) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(JsonResponse(200, R"({"data": []})")));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request,
                       fixture.client->Start(SpecFor("a mossy boulder")));

  EXPECT_EQ(request.Poll().status().code(), absl::StatusCode::kNotFound);
}

// A missing key must fail the request, not the engine that owns the client.
TEST(OpenAiImageClientTest, ReportsAMissingCredentialAsUnauthenticated) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture,
                       MakeClient(JsonResponse(200, SuccessBody(1)), /*credential=*/""));

  const absl::Status status = fixture.client->Start(SpecFor("a mossy boulder")).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kUnauthenticated);
}

TEST(OpenAiImageClientTest, RejectsAnIncompleteConfiguration) {
  FakeTransport transport{JsonResponse(200, "{}")};
  FakeCredentials credentials{"sk-test"};

  OpenAiImageConfig no_model;
  no_model.model.clear();
  EXPECT_EQ(OpenAiImageClient::Create(transport, credentials, no_model).status().code(),
            absl::StatusCode::kInvalidArgument);

  OpenAiImageConfig no_limit;
  no_limit.maximum_candidate_pixels = 0;
  EXPECT_EQ(OpenAiImageClient::Create(transport, credentials, no_limit).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
