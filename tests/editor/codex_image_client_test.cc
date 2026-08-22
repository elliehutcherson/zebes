#include "editor/image_generation/codex_image_client.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "editor/image_generation/codex_app_server_transport.h"
#include "editor/image_generation/image_generation.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

enum class FakeCompletion : uint8_t {
  kSuccess = 0,
  kPending,
  kInterruptFailure,
  kUsageLimit,
  kEscapedPath,
  kCodexManagedPath,
  kApprovalRequest,
};

class FakeAppServerTransport final : public CodexAppServerTransport {
 public:
  FakeAppServerTransport(std::filesystem::path working_directory,
                         std::filesystem::path generated_images_directory, std::string account_type,
                         FakeCompletion completion)
      : working_directory_(std::move(working_directory)),
        generated_images_directory_(std::move(generated_images_directory)),
        account_type_(std::move(account_type)),
        completion_(completion) {}

  absl::Status Start() override {
    ++start_count_;
    return absl::OkStatus();
  }

  absl::Status SendLine(std::string line) override {
    nlohmann::json message = nlohmann::json::parse(line);
    sent_.push_back(message);
    if (!message.contains("id")) return absl::OkStatus();

    const int64_t id = message.at("id").get<int64_t>();
    const std::string method = message.at("method").get<std::string>();
    if (method == "initialize") {
      Queue({{"id", id}, {"result", {{"userAgent", "fake-codex/1.0"}}}});
      return absl::OkStatus();
    }
    if (method == "account/read") {
      Queue(
          {{"id", id}, {"result", {{"account", {{"type", account_type_}, {"planType", "pro"}}}}}});
      return absl::OkStatus();
    }
    if (method == "skills/list") {
      Queue(
          {{"id", id},
           {"result",
            {{"data", nlohmann::json::array(
                          {{{"cwd", working_directory_.string()},
                            {"skills", nlohmann::json::array({{{"name", "imagegen"},
                                                               {"path", "/fake/imagegen/SKILL.md"},
                                                               {"enabled", true}}})}}})}}}});
      return absl::OkStatus();
    }
    if (method == "thread/start") {
      const std::string thread_id = "thread-" + std::to_string(++thread_count_);
      Queue({{"id", id}, {"result", {{"thread", {{"id", thread_id}}}}}});
      return absl::OkStatus();
    }
    if (method == "turn/start") {
      const std::string turn_id = "turn-" + std::to_string(++turn_count_);
      Queue({{"id", id}, {"result", {{"turn", {{"id", turn_id}}}}}});
      if (completion_ == FakeCompletion::kPending ||
          completion_ == FakeCompletion::kInterruptFailure) {
        return absl::OkStatus();
      }
      if (completion_ == FakeCompletion::kApprovalRequest) {
        Queue({{"id", 900},
               {"method", "item/fileChange/requestApproval"},
               {"params", nlohmann::json::object()}});
        return absl::OkStatus();
      }
      QueueImageCompletion(message.at("params").at("threadId").get<std::string>(), turn_id);
      return absl::OkStatus();
    }
    if (method == "turn/interrupt") {
      if (completion_ == FakeCompletion::kInterruptFailure) {
        return absl::UnavailableError("fake interrupt write failed");
      }
      Queue({{"id", id}, {"result", nlohmann::json::object()}});
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError("fake received an unexpected method");
  }

  absl::StatusOr<std::vector<std::string>> Poll() override {
    std::vector<std::string> available = std::move(incoming_);
    incoming_.clear();
    return available;
  }

  void Stop() noexcept override { stopped_ = true; }

  const std::filesystem::path& working_directory() const override { return working_directory_; }

  std::string diagnostics() const override { return {}; }

  int start_count() const { return start_count_; }
  bool stopped() const { return stopped_; }
  const std::vector<nlohmann::json>& sent() const { return sent_; }

 private:
  void Queue(nlohmann::json message) { incoming_.push_back(message.dump()); }

  void QueueImageCompletion(const std::string& thread_id, const std::string& turn_id) {
    std::filesystem::path saved_path = working_directory_ / ("generated-" + turn_id + ".png");
    if (completion_ == FakeCompletion::kEscapedPath) {
      saved_path = working_directory_.parent_path() / "zebes_codex_escape.png";
    } else if (completion_ == FakeCompletion::kCodexManagedPath) {
      saved_path = generated_images_directory_ / "thread-1" / "generated-turn-1.png";
    }
    nlohmann::json item{{"type", "imageGeneration"},
                        {"id", "image-1"},
                        {"status", "completed"},
                        {"savedPath", saved_path.string()},
                        {"revisedPrompt", "a revised mossy boulder"},
                        {"failure", nullptr}};
    if (completion_ == FakeCompletion::kUsageLimit) {
      item["status"] = "failed";
      item["failure"] = {{"type", "usageLimitExceeded"}, {"resetsAt", 1234}};
    }
    Queue({{"method", "item/completed"},
           {"params", {{"threadId", thread_id}, {"turnId", turn_id}, {"item", item}}}});
    Queue({{"method", "turn/completed"},
           {"params",
            {{"threadId", thread_id}, {"turn", {{"id", turn_id}, {"status", "completed"}}}}}});
  }

  std::filesystem::path working_directory_;
  std::filesystem::path generated_images_directory_;
  std::string account_type_;
  FakeCompletion completion_;
  int start_count_ = 0;
  int thread_count_ = 0;
  int turn_count_ = 0;
  bool stopped_ = false;
  std::vector<nlohmann::json> sent_;
  std::vector<std::string> incoming_;
};

std::filesystem::path UniqueTestDirectory(std::string_view name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / std::string(name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

void WriteCandidate(const std::filesystem::path& path) {
  std::vector<uint8_t> pixels(4 * 4 * 4, 0);
  for (size_t index = 0; index < pixels.size(); index += 4) {
    pixels[index] = 40;
    pixels[index + 1] = 90;
    pixels[index + 2] = 60;
    pixels[index + 3] = 255;
  }
  ASSERT_OK(WritePng(path.string(), 4, 4, pixels));
}

ImageGenerationSpec SpecFor(std::string prompt = "a mossy boulder") {
  return ImageGenerationSpec{
      .prompt = std::move(prompt),
      .target_aspect = {.width = 3, .height = 2},
  };
}

absl::StatusOr<ImageGenerationResult> PollToCompletion(ImageGenerationRequest& request) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    ASSIGN_OR_RETURN(std::optional<ImageGenerationResult> result, request.Poll());
    if (result.has_value()) return *std::move(result);
  }
  return absl::DeadlineExceededError("fake Codex request did not finish");
}

struct Fixture {
  std::filesystem::path working_directory;
  std::filesystem::path generated_images_directory;
  FakeAppServerTransport* transport = nullptr;
  std::unique_ptr<CodexImageClient> client;
};

absl::StatusOr<Fixture> MakeClient(FakeCompletion completion = FakeCompletion::kSuccess,
                                   std::string account_type = "chatgpt") {
  Fixture fixture;
  fixture.working_directory = UniqueTestDirectory("zebes_codex_image_client_test");
  fixture.generated_images_directory = UniqueTestDirectory("zebes_codex_generated_images_test");
  for (int turn = 1; turn <= 8; ++turn) {
    WriteCandidate(fixture.working_directory / ("generated-turn-" + std::to_string(turn) + ".png"));
  }
  auto transport = std::make_unique<FakeAppServerTransport>(fixture.working_directory,
                                                            fixture.generated_images_directory,
                                                            std::move(account_type), completion);
  fixture.transport = transport.get();
  CodexImageConfig config;
  config.generated_images_directory = fixture.generated_images_directory;
  ASSIGN_OR_RETURN(fixture.client,
                   CodexImageClient::CreateWithTransport(std::move(transport), std::move(config)));
  return fixture;
}

const nlohmann::json* FindSent(const FakeAppServerTransport& transport, std::string_view method) {
  for (const nlohmann::json& message : transport.sent()) {
    if (message.at("method").get<std::string>() == method) return &message;
  }
  return nullptr;
}

TEST(CodexImageClientTest, LazilyGeneratesThroughAConfinedChatGptSession) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient());
  EXPECT_EQ(fixture.transport->start_count(), 0);

  ImageGenerationSpec spec = SpecFor();
  spec.instructions = "Create one isolated prop.";
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(std::move(spec)));
  EXPECT_EQ(fixture.transport->start_count(), 1);
  ASSERT_OK_AND_ASSIGN(ImageGenerationResult result, PollToCompletion(request));

  EXPECT_EQ(result.provider, "openai-codex");
  EXPECT_EQ(result.model, "codex-imagegen");
  EXPECT_EQ(result.submitted_prompt, "a mossy boulder");
  ASSERT_TRUE(result.provider_request_id.has_value());
  EXPECT_EQ(*result.provider_request_id, "turn-1");
  ASSERT_EQ(result.candidates.size(), 1);
  EXPECT_EQ(result.candidates[0].image.width, 4);
  EXPECT_EQ(result.candidates[0].image.height, 4);
  EXPECT_EQ(result.candidates[0].revised_prompt, "a revised mossy boulder");
  EXPECT_FALSE(std::filesystem::exists(fixture.working_directory / "generated-turn-1.png"));

  const nlohmann::json* thread = FindSent(*fixture.transport, "thread/start");
  ASSERT_NE(thread, nullptr);
  EXPECT_TRUE(thread->at("params").at("ephemeral"));
  EXPECT_EQ(thread->at("params").at("approvalPolicy"), "never");
  EXPECT_EQ(thread->at("params").at("sandbox"), "workspace-write");
  EXPECT_EQ(thread->at("params").at("cwd"), fixture.working_directory.string());
  EXPECT_NE(thread->at("params")
                .at("developerInstructions")
                .get<std::string>()
                .find("Artwork requirements:\nCreate one isolated prop."),
            std::string::npos);

  const nlohmann::json* turn = FindSent(*fixture.transport, "turn/start");
  ASSERT_NE(turn, nullptr);
  const nlohmann::json& input = turn->at("params").at("input");
  EXPECT_EQ(input.at(1).at("type"), "skill");
  EXPECT_EQ(input.at(1).at("name"), "imagegen");
  const std::string request_text = input.at(0).at("text").get<std::string>();
  EXPECT_NE(request_text.find("a mossy boulder"), std::string::npos);
  EXPECT_NE(request_text.find("3:2"), std::string::npos);
}

TEST(CodexImageClientTest, RefusesApiKeyAuthenticationBeforeStartingAThread) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture,
                       MakeClient(FakeCompletion::kSuccess, /*account_type=*/"apiKey"));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(SpecFor()));

  const absl::Status status = PollToCompletion(request).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(FindSent(*fixture.transport, "thread/start"), nullptr);
}

TEST(CodexImageClientTest, ReportsImageGenerationUsageLimits) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(FakeCompletion::kUsageLimit));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(SpecFor()));

  const absl::Status status = PollToCompletion(request).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
}

TEST(CodexImageClientTest, RejectsImagesOutsideTheTrustedOutputDirectories) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(FakeCompletion::kEscapedPath));
  const std::filesystem::path escaped =
      fixture.working_directory.parent_path() / "zebes_codex_escape.png";
  WriteCandidate(escaped);
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(SpecFor()));

  const absl::Status status = PollToCompletion(request).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kPermissionDenied);
  std::filesystem::remove(escaped);
}

TEST(CodexImageClientTest, ReadsButDoesNotDeleteImagesFromTheCodexManagedCache) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(FakeCompletion::kCodexManagedPath));
  const std::filesystem::path generated =
      fixture.generated_images_directory / "thread-1" / "generated-turn-1.png";
  std::filesystem::create_directories(generated.parent_path());
  WriteCandidate(generated);
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(SpecFor()));

  ASSERT_OK_AND_ASSIGN(ImageGenerationResult result, PollToCompletion(request));

  ASSERT_EQ(result.candidates.size(), 1);
  EXPECT_TRUE(result.candidates[0].image.IsValid());
  EXPECT_TRUE(std::filesystem::exists(generated));
  std::filesystem::remove_all(fixture.generated_images_directory);
}

TEST(CodexImageClientTest, RefusesApprovalRequestsFromTheAppServer) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(FakeCompletion::kApprovalRequest));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(SpecFor()));

  const absl::Status status = PollToCompletion(request).status();

  EXPECT_EQ(status.code(), absl::StatusCode::kPermissionDenied);
}

TEST(CodexImageClientTest, MultiplexesOperationsOverOneAppServerProcess) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient());
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest first,
                       fixture.client->Start(SpecFor("first boulder")));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest second,
                       fixture.client->Start(SpecFor("second boulder")));

  ASSERT_OK_AND_ASSIGN(ImageGenerationResult first_result, PollToCompletion(first));
  ASSERT_OK_AND_ASSIGN(ImageGenerationResult second_result, PollToCompletion(second));

  EXPECT_EQ(fixture.transport->start_count(), 1);
  EXPECT_EQ(first_result.submitted_prompt, "first boulder");
  EXPECT_EQ(second_result.submitted_prompt, "second boulder");
  EXPECT_NE(first_result.provider_request_id, second_result.provider_request_id);
}

TEST(CodexImageClientTest, CancellationInterruptsAnActiveTurnWithoutStoppingTheSession) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(FakeCompletion::kPending));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(SpecFor()));
  for (int attempt = 0; attempt < 10 && FindSent(*fixture.transport, "turn/start") == nullptr;
       ++attempt) {
    ASSERT_OK(request.Poll().status());
  }
  ASSERT_NE(FindSent(*fixture.transport, "turn/start"), nullptr);
  ASSERT_OK(request.Poll().status());

  request.Cancel();

  EXPECT_FALSE(request.active());
  const nlohmann::json* interrupt = FindSent(*fixture.transport, "turn/interrupt");
  ASSERT_NE(interrupt, nullptr);
  EXPECT_EQ(interrupt->at("params").at("threadId"), "thread-1");
  EXPECT_EQ(interrupt->at("params").at("turnId"), "turn-1");
  EXPECT_FALSE(fixture.transport->stopped());
}

TEST(CodexImageClientTest, CancellationBeforeAThreadImmediatelyReleasesTheOperation) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient());
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest cancelled,
                       fixture.client->Start(SpecFor("cancelled boulder")));

  cancelled.Cancel();

  EXPECT_FALSE(cancelled.active());
  EXPECT_EQ(FindSent(*fixture.transport, "thread/start"), nullptr);
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest active,
                       fixture.client->Start(SpecFor("retained boulder")));
  ASSERT_OK_AND_ASSIGN(ImageGenerationResult result, PollToCompletion(active));
  EXPECT_EQ(result.submitted_prompt, "retained boulder");
}

TEST(CodexImageClientTest, InterruptSendFailurePermanentlyFailsTheSharedSession) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture, MakeClient(FakeCompletion::kInterruptFailure));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest cancelled,
                       fixture.client->Start(SpecFor("cancelled boulder")));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest affected,
                       fixture.client->Start(SpecFor("affected boulder")));
  for (int attempt = 0; attempt < 10 && FindSent(*fixture.transport, "turn/start") == nullptr;
       ++attempt) {
    ASSERT_OK(cancelled.Poll().status());
  }
  ASSERT_NE(FindSent(*fixture.transport, "turn/start"), nullptr);
  ASSERT_OK(cancelled.Poll().status());

  cancelled.Cancel();

  EXPECT_TRUE(fixture.transport->stopped());
  EXPECT_EQ(affected.Poll().status().code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(fixture.client->Start(SpecFor("later boulder")).status().code(),
            absl::StatusCode::kUnavailable);
}

TEST(CodexImageClientTest, SessionFailureIsPermanent) {
  ASSERT_OK_AND_ASSIGN(Fixture fixture,
                       MakeClient(FakeCompletion::kSuccess, /*account_type=*/"apiKey"));
  ASSERT_OK_AND_ASSIGN(ImageGenerationRequest request, fixture.client->Start(SpecFor()));
  EXPECT_EQ(PollToCompletion(request).status().code(), absl::StatusCode::kUnauthenticated);

  EXPECT_EQ(fixture.client->Start(SpecFor("another boulder")).status().code(),
            absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(fixture.transport->start_count(), 1);
}

TEST(CodexImageClientTest, RejectsInvalidConfiguration) {
  std::filesystem::path working_directory =
      UniqueTestDirectory("zebes_codex_image_client_config_test");
  auto transport = std::make_unique<FakeAppServerTransport>(working_directory,
                                                            working_directory / "generated_images",
                                                            "chatgpt", FakeCompletion::kSuccess);
  CodexImageConfig config;
  config.maximum_candidate_pixels = 0;

  EXPECT_EQ(CodexImageClient::CreateWithTransport(std::move(transport), config).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
