#include "generation/codex_app_server_protocol.h"

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

int64_t RequestId(const absl::StatusOr<std::string>& encoded) {
  EXPECT_OK(encoded.status());
  return nlohmann::json::parse(*encoded).at("id").get<int64_t>();
}

TEST(CodexAppServerProtocolTest, CorrelatesResponsesIntoTypedModels) {
  CodexAppServerProtocol protocol;

  const int64_t account_id = RequestId(protocol.ReadAccount());
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent account_event,
      protocol.Parse(
          nlohmann::json({{"id", account_id}, {"result", {{"account", {{"type", "chatgpt"}}}}}})
              .dump()));
  const auto* account = std::get_if<CodexAccountRead>(&account_event);
  ASSERT_NE(account, nullptr);
  ASSERT_TRUE(account->type.has_value());
  EXPECT_EQ(*account->type, "chatgpt");

  const int64_t models_id = RequestId(protocol.ListModels(std::nullopt));
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent models_event,
      protocol.Parse(
          nlohmann::json(
              {{"id", models_id},
               {"result",
                {{"data", nlohmann::json::array({{{"id", "model-1"}, {"model", "gpt-5.6-sol"}},
                                                 {{"id", "model-2"}, {"model", "gpt-5.6-luna"}}})},
                 {"nextCursor", "page-2"}}}})
              .dump()));
  const auto* models = std::get_if<CodexModelsListed>(&models_event);
  ASSERT_NE(models, nullptr);
  EXPECT_EQ(models->models, (std::vector<std::string>{"gpt-5.6-sol", "gpt-5.6-luna"}));
  ASSERT_TRUE(models->next_cursor.has_value());
  EXPECT_EQ(*models->next_cursor, "page-2");

  ASSERT_OK_AND_ASSIGN(const std::string next_models, protocol.ListModels(models->next_cursor));
  const nlohmann::json next_request = nlohmann::json::parse(next_models);
  EXPECT_EQ(next_request.at("method"), "model/list");
  EXPECT_EQ(next_request.at("params").at("cursor"), "page-2");

  const std::filesystem::path cwd = "/private/tmp/zebes-protocol-test";
  const int64_t skills_id = RequestId(protocol.ListSkills(cwd));
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent skills_event,
      protocol.Parse(
          nlohmann::json(
              {{"id", skills_id},
               {"result",
                {{"data",
                  nlohmann::json::array(
                      {{{"cwd", cwd.string()},
                        {"skills", nlohmann::json::array({{{"name", "imagegen"},
                                                           {"path", "/skills/imagegen/SKILL.md"},
                                                           {"enabled", true}}})}}})}}}})
              .dump()));
  const auto* skills = std::get_if<CodexSkillsListed>(&skills_event);
  ASSERT_NE(skills, nullptr);
  ASSERT_EQ(skills->directories.size(), 1);
  ASSERT_EQ(skills->directories[0].skills.size(), 1);
  EXPECT_EQ(skills->directories[0].cwd, cwd);
  EXPECT_EQ(skills->directories[0].skills[0].name, "imagegen");
  EXPECT_TRUE(skills->directories[0].skills[0].enabled);
}

TEST(CodexAppServerProtocolTest, EncodesTurnInputsAndTracksTheirOperation) {
  CodexAppServerProtocol protocol;
  ASSERT_OK_AND_ASSIGN(
      const std::string encoded_thread,
      protocol.StartThread(42, "/private/tmp", "codex-image-model", "Create one isolated prop."));
  const nlohmann::json thread_request = nlohmann::json::parse(encoded_thread);
  EXPECT_EQ(
      thread_request,
      nlohmann::json({
          {"method", "thread/start"},
          {"id", 1},
          {"params",
           {{"cwd", "/private/tmp"},
            {"ephemeral", true},
            {"approvalPolicy", "never"},
            {"sandbox", "workspace-write"},
            {"model", "codex-image-model"},
            {"allowProviderModelFallback", false},
            {"developerInstructions",
             "Act only as an image-generation worker. Use the explicitly supplied imagegen skill, "
             "do not inspect unrelated files, do not run shell commands, generate exactly one "
             "image, and then stop.\n\nArtwork requirements:\nCreate one isolated prop."}}},
      }));

  ASSERT_OK_AND_ASSIGN(
      const std::string encoded,
      protocol.StartTurn(42, "thread-7", "a mossy boulder", "/skills/imagegen/SKILL.md",
                         std::vector<std::filesystem::path>{"/private/tmp/reference-1.png",
                                                            "/private/tmp/reference-2.png"}));
  const nlohmann::json request = nlohmann::json::parse(encoded);

  EXPECT_EQ(request.at("method"), "turn/start");
  EXPECT_EQ(request.at("params").at("threadId"), "thread-7");
  EXPECT_EQ(
      request.at("params").at("input"),
      nlohmann::json::array(
          {{{"type", "localImage"}, {"path", "/private/tmp/reference-1.png"}},
           {{"type", "localImage"}, {"path", "/private/tmp/reference-2.png"}},
           {{"type", "text"}, {"text", "a mossy boulder"}},
           {{"type", "skill"}, {"name", "imagegen"}, {"path", "/skills/imagegen/SKILL.md"}}}));

  ASSERT_OK_AND_ASSIGN(CodexProtocolEvent event,
                       protocol.Parse(nlohmann::json({{"id", request.at("id")},
                                                      {"result", {{"turn", {{"id", "turn-8"}}}}}})
                                          .dump()));
  const auto* started = std::get_if<CodexTurnStarted>(&event);
  ASSERT_NE(started, nullptr);
  EXPECT_EQ(started->operation_id, 42);
  EXPECT_EQ(started->turn_id, "turn-8");
}

TEST(CodexAppServerProtocolTest, DistinguishesSessionAndOperationRequestFailures) {
  CodexAppServerProtocol protocol;
  const int64_t account_id = RequestId(protocol.ReadAccount());
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent account_event,
      protocol.Parse(
          nlohmann::json({{"id", account_id}, {"error", {{"message", "not authenticated"}}}})
              .dump()));
  const auto* session_failure = std::get_if<CodexSessionRequestFailed>(&account_event);
  ASSERT_NE(session_failure, nullptr);
  EXPECT_EQ(session_failure->kind, CodexRequestKind::kReadAccount);
  EXPECT_EQ(session_failure->detail, "not authenticated");

  const int64_t thread_id =
      RequestId(protocol.StartThread(42, "/private/tmp", "codex-image-model", std::nullopt));
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent thread_event,
      protocol.Parse(
          nlohmann::json({{"id", thread_id}, {"error", {{"message", "thread refused"}}}}).dump()));
  const auto* operation_failure = std::get_if<CodexOperationRequestFailed>(&thread_event);
  ASSERT_NE(operation_failure, nullptr);
  EXPECT_EQ(operation_failure->kind, CodexRequestKind::kStartThread);
  EXPECT_EQ(operation_failure->operation_id, 42);
  EXPECT_EQ(operation_failure->detail, "thread refused");
}

TEST(CodexAppServerProtocolTest, TranslatesImageFailuresAndRejectsUnknownResponses) {
  CodexAppServerProtocol protocol;
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent event,
      protocol.Parse(nlohmann::json({{"method", "item/completed"},
                                     {"params",
                                      {{"turnId", "turn-9"},
                                       {"item",
                                        {{"type", "imageGeneration"},
                                         {"status", "failed"},
                                         {"failure", {{"type", "usageLimitExceeded"}}}}}}}})
                         .dump()));
  const auto* image = std::get_if<CodexImageFailed>(&event);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->turn_id, "turn-9");
  EXPECT_EQ(image->failure, CodexImageFailure::kUsageLimitExceeded);

  EXPECT_EQ(protocol.Parse(R"({"id":999,"result":{}})").status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(protocol.Parse("not-json").status().code(), absl::StatusCode::kDataLoss);
}

TEST(CodexAppServerProtocolTest, PreservesTurnFailureDetail) {
  CodexAppServerProtocol protocol;
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent event,
      protocol.Parse(
          R"({"method":"turn/completed","params":{"threadId":"thread-7","turn":{"id":"turn-12","status":"failed","items":[],"error":{"message":"requested model is unavailable","additionalDetails":"codex-imagegen"}}}})"));

  const auto* failed = std::get_if<CodexTurnFailed>(&event);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->turn_id, "turn-12");
  EXPECT_EQ(failed->status, "failed");
  EXPECT_EQ(failed->detail, "requested model is unavailable: codex-imagegen");
}

TEST(CodexAppServerProtocolTest, ConstructsOnlyCompleteImageSuccessModels) {
  CodexAppServerProtocol protocol;
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent event,
      protocol.Parse(
          R"({"method":"item/completed","params":{"turnId":"turn-10","item":{"type":"imageGeneration","status":"completed","savedPath":"/private/tmp/image.png","revisedPrompt":"mossy stone"}}})"));

  const auto* image = std::get_if<CodexImageSucceeded>(&event);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->turn_id, "turn-10");
  EXPECT_EQ(image->saved_path, "/private/tmp/image.png");
  EXPECT_EQ(image->revised_prompt, "mossy stone");

  EXPECT_EQ(
      protocol
          .Parse(
              R"({"method":"item/completed","params":{"turnId":"turn-11","item":{"type":"imageGeneration","status":"completed"}}})")
          .status()
          .code(),
      absl::StatusCode::kDataLoss);
}

TEST(CodexAppServerProtocolTest, ExposesServerRequestsWithoutProviderJson) {
  CodexAppServerProtocol protocol;
  ASSERT_OK_AND_ASSIGN(
      CodexProtocolEvent event,
      protocol.Parse(R"({"id":12,"method":"item/fileChange/requestApproval","params":{}})"));

  const auto* request = std::get_if<CodexServerRequest>(&event);
  ASSERT_NE(request, nullptr);
  EXPECT_EQ(request->method, "item/fileChange/requestApproval");
}

}  // namespace
}  // namespace zebes
