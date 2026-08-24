#include "editor/image_generation/codex_app_server_protocol.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

absl::Status JsonError(std::string_view context, const nlohmann::json::exception& error) {
  return absl::DataLossError(absl::StrCat(context, ": ", error.what()));
}

template <typename Function>
auto TranslateJson(std::string_view context, Function&& translate) -> decltype(translate()) {
  try {
    return translate();
  } catch (const nlohmann::json::exception& error) {
    return JsonError(context, error);
  }
}

absl::StatusOr<std::string> RequiredString(const nlohmann::json& object, std::string_view key,
                                           std::string_view context) {
  const nlohmann::json& value = object.at(std::string(key));
  if (!value.is_string()) {
    return absl::DataLossError(absl::StrCat(context, " has a non-string ", key));
  }
  const std::string result = value.get<std::string>();
  if (result.empty()) {
    return absl::DataLossError(absl::StrCat(context, " has an empty ", key));
  }
  return result;
}

std::string ErrorDetail(const nlohmann::json& error) {
  if (!error.is_object() || !error.contains("message") || !error.at("message").is_string()) {
    return "Codex App Server rejected a request";
  }
  return error.at("message").get<std::string>();
}

absl::StatusOr<CodexAccountRead> ParseAccount(const nlohmann::json& result) {
  if (!result.contains("account") || result.at("account").is_null()) {
    return CodexAccountRead{};
  }
  if (!result.at("account").is_object()) {
    return absl::DataLossError("Codex account is not an object");
  }
  ASSIGN_OR_RETURN(const std::string type,
                   RequiredString(result.at("account"), "type", "Codex account"));
  return CodexAccountRead{.type = type};
}

absl::StatusOr<CodexSkillsListed> ParseSkills(const nlohmann::json& result) {
  if (!result.contains("data") || !result.at("data").is_array()) {
    return absl::DataLossError("Codex skills response has no data array");
  }
  CodexSkillsListed listed;
  for (const nlohmann::json& entry : result.at("data")) {
    if (!entry.is_object() || !entry.contains("skills") || !entry.at("skills").is_array()) {
      return absl::DataLossError("Codex skill directory is malformed");
    }
    ASSIGN_OR_RETURN(const std::string cwd, RequiredString(entry, "cwd", "Codex skill directory"));
    CodexSkillDirectory directory{.cwd = cwd};
    for (const nlohmann::json& skill : entry.at("skills")) {
      if (!skill.is_object() || !skill.contains("enabled") || !skill.at("enabled").is_boolean()) {
        return absl::DataLossError("Codex skill is malformed");
      }
      ASSIGN_OR_RETURN(const std::string name, RequiredString(skill, "name", "Codex skill"));
      ASSIGN_OR_RETURN(const std::string path, RequiredString(skill, "path", "Codex skill"));
      directory.skills.push_back(CodexSkill{
          .name = name,
          .path = path,
          .enabled = skill.at("enabled").get<bool>(),
      });
    }
    listed.directories.push_back(std::move(directory));
  }
  return listed;
}

absl::StatusOr<CodexProtocolEvent> ParseImageCompleted(const nlohmann::json& params) {
  if (!params.is_object() || !params.contains("item") || !params.at("item").is_object()) {
    return absl::DataLossError("Codex item completion has no item");
  }
  ASSIGN_OR_RETURN(const std::string turn_id,
                   RequiredString(params, "turnId", "Codex image completion"));
  const nlohmann::json& item = params.at("item");
  ASSIGN_OR_RETURN(const std::string status,
                   RequiredString(item, "status", "Codex image generation"));

  if (item.contains("failure") && !item.at("failure").is_null()) {
    const nlohmann::json& failure = item.at("failure");
    CodexImageFailure failure_type = CodexImageFailure::kOther;
    if (failure.is_object() && failure.contains("type") && failure.at("type").is_string() &&
        failure.at("type").get<std::string>() == "usageLimitExceeded") {
      failure_type = CodexImageFailure::kUsageLimitExceeded;
    }
    return CodexImageFailed{.turn_id = turn_id, .status = status, .failure = failure_type};
  }
  if (status != "completed") {
    return CodexImageFailed{
        .turn_id = turn_id, .status = status, .failure = CodexImageFailure::kOther};
  }

  ASSIGN_OR_RETURN(const std::string saved_path,
                   RequiredString(item, "savedPath", "Codex image generation"));
  CodexImageSucceeded completed{.turn_id = turn_id, .saved_path = saved_path};
  if (item.contains("revisedPrompt") && !item.at("revisedPrompt").is_null()) {
    if (!item.at("revisedPrompt").is_string()) {
      return absl::DataLossError("Codex image generation has a non-string revised prompt");
    }
    const std::string revised = item.at("revisedPrompt").get<std::string>();
    if (!revised.empty()) completed.revised_prompt = revised;
  }
  return completed;
}

absl::StatusOr<CodexProtocolEvent> ParseTurnCompleted(const nlohmann::json& params) {
  if (!params.is_object() || !params.contains("turn") || !params.at("turn").is_object()) {
    return absl::DataLossError("Codex turn completion has no turn");
  }
  ASSIGN_OR_RETURN(const std::string turn_id,
                   RequiredString(params.at("turn"), "id", "Codex turn"));
  ASSIGN_OR_RETURN(const std::string status,
                   RequiredString(params.at("turn"), "status", "Codex turn"));
  if (status == "completed") return CodexTurnSucceeded{.turn_id = turn_id};
  return CodexTurnFailed{.turn_id = turn_id, .status = status};
}

}  // namespace

absl::StatusOr<std::string> CodexAppServerProtocol::EncodeSessionRequest(CodexRequestKind kind,
                                                                         std::string_view method,
                                                                         std::string params_json) {
  return EncodeRequest(PendingRequest{.kind = kind}, method, std::move(params_json));
}

absl::StatusOr<std::string> CodexAppServerProtocol::EncodeOperationRequest(
    CodexRequestKind kind, uint64_t operation_id, std::string_view method,
    std::string params_json) {
  return EncodeRequest(PendingRequest{.kind = kind, .operation_id = operation_id}, method,
                       std::move(params_json));
}

absl::StatusOr<std::string> CodexAppServerProtocol::EncodeRequest(PendingRequest pending,
                                                                  std::string_view method,
                                                                  std::string params_json) {
  const int64_t request_id = next_request_id_++;
  pending_.try_emplace(request_id, std::move(pending));
  return absl::StrCat(R"({"method":")", method, R"(","id":)", request_id, R"(,"params":)",
                      params_json, "}");
}

absl::StatusOr<std::string> CodexAppServerProtocol::Initialize() {
  return EncodeSessionRequest(
      CodexRequestKind::kInitialize, "initialize",
      R"({"clientInfo":{"name":"zebes","title":"Zebes Image Generation","version":"0.1.0"}})");
}

absl::StatusOr<std::string> CodexAppServerProtocol::InitializedNotification() {
  return R"({"method":"initialized","params":{}})";
}

absl::StatusOr<std::string> CodexAppServerProtocol::ReadAccount() {
  return EncodeSessionRequest(CodexRequestKind::kReadAccount, "account/read",
                              R"({"refreshToken":false})");
}

absl::StatusOr<std::string> CodexAppServerProtocol::ListSkills(const std::filesystem::path& cwd) {
  return TranslateJson("could not encode Codex skills request", [&] {
    return EncodeSessionRequest(
        CodexRequestKind::kListSkills, "skills/list",
        nlohmann::json{{"cwds", nlohmann::json::array({cwd.string()})}, {"forceReload", false}}
            .dump());
  });
}

absl::StatusOr<std::string> CodexAppServerProtocol::StartThread(
    uint64_t operation_id, const std::filesystem::path& cwd,
    const std::optional<std::string>& generation_instructions) {
  return TranslateJson("could not encode Codex thread request", [&] {
    std::string developer_instructions =
        "Act only as an image-generation worker. Use the explicitly supplied imagegen skill, "
        "do not inspect unrelated files, do not run shell commands, generate exactly one "
        "image, and then stop.";
    if (generation_instructions.has_value()) {
      absl::StrAppend(&developer_instructions, "\n\nArtwork requirements:\n",
                      *generation_instructions);
    }
    const nlohmann::json params{{"cwd", cwd.string()},
                                {"ephemeral", true},
                                {"approvalPolicy", "never"},
                                {"sandbox", "workspace-write"},
                                {"developerInstructions", developer_instructions}};
    return EncodeOperationRequest(CodexRequestKind::kStartThread, operation_id, "thread/start",
                                  params.dump());
  });
}

absl::StatusOr<std::string> CodexAppServerProtocol::StartTurn(
    uint64_t operation_id, std::string_view thread_id, std::string_view prompt,
    const std::filesystem::path& skill_path) {
  return TranslateJson("could not encode Codex turn request", [&] {
    const nlohmann::json input = nlohmann::json::array(
        {{{"type", "text"}, {"text", prompt}},
         {{"type", "skill"}, {"name", "imagegen"}, {"path", skill_path.string()}}});
    return EncodeOperationRequest(CodexRequestKind::kStartTurn, operation_id, "turn/start",
                                  nlohmann::json{{"threadId", thread_id}, {"input", input}}.dump());
  });
}

absl::StatusOr<std::string> CodexAppServerProtocol::InterruptTurn(uint64_t operation_id,
                                                                  std::string_view thread_id,
                                                                  std::string_view turn_id) {
  return TranslateJson("could not encode Codex interrupt request", [&] {
    return EncodeOperationRequest(
        CodexRequestKind::kInterruptTurn, operation_id, "turn/interrupt",
        nlohmann::json{{"threadId", thread_id}, {"turnId", turn_id}}.dump());
  });
}

absl::StatusOr<CodexProtocolEvent> CodexAppServerProtocol::Parse(std::string_view line) {
  return TranslateJson(
      "could not translate Codex protocol message", [&]() -> absl::StatusOr<CodexProtocolEvent> {
        nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
        if (message.is_discarded() || !message.is_object()) {
          return absl::DataLossError("Codex App Server emitted malformed JSON");
        }
        if (message.contains("method") && message.contains("id")) {
          ASSIGN_OR_RETURN(const std::string method,
                           RequiredString(message, "method", "Codex server request"));
          return CodexServerRequest{.method = method};
        }
        if (message.contains("id")) {
          if (!message.at("id").is_number_integer()) {
            return absl::DataLossError("Codex response id is not an integer");
          }
          const int64_t request_id = message.at("id").get<int64_t>();
          const auto found = pending_.find(request_id);
          if (found == pending_.end()) {
            return absl::DataLossError("Codex response has an unknown request id");
          }
          const PendingRequest pending = found->second;
          pending_.erase(found);
          if (message.contains("error")) {
            const std::string detail = ErrorDetail(message.at("error"));
            if (pending.operation_id.has_value()) {
              return CodexOperationRequestFailed{
                  .kind = pending.kind, .operation_id = *pending.operation_id, .detail = detail};
            }
            return CodexSessionRequestFailed{.kind = pending.kind, .detail = detail};
          }
          if (!message.contains("result") || !message.at("result").is_object()) {
            return absl::DataLossError("Codex response has no object result");
          }
          const nlohmann::json& result = message.at("result");
          switch (pending.kind) {
            case CodexRequestKind::kInitialize:
              return CodexInitialized{};
            case CodexRequestKind::kReadAccount: {
              ASSIGN_OR_RETURN(CodexAccountRead account, ParseAccount(result));
              return account;
            }
            case CodexRequestKind::kListSkills: {
              ASSIGN_OR_RETURN(CodexSkillsListed skills, ParseSkills(result));
              return skills;
            }
            case CodexRequestKind::kStartThread: {
              if (!pending.operation_id.has_value()) {
                return absl::InternalError("Codex thread request has no operation id");
              }
              if (!result.contains("thread") || !result.at("thread").is_object()) {
                return absl::DataLossError("Codex thread response has no thread");
              }
              ASSIGN_OR_RETURN(const std::string thread_id,
                               RequiredString(result.at("thread"), "id", "Codex thread"));
              return CodexThreadStarted{.operation_id = *pending.operation_id,
                                        .thread_id = thread_id};
            }
            case CodexRequestKind::kStartTurn: {
              if (!pending.operation_id.has_value()) {
                return absl::InternalError("Codex turn request has no operation id");
              }
              if (!result.contains("turn") || !result.at("turn").is_object()) {
                return absl::DataLossError("Codex turn response has no turn");
              }
              ASSIGN_OR_RETURN(const std::string turn_id,
                               RequiredString(result.at("turn"), "id", "Codex turn"));
              return CodexTurnStarted{.operation_id = *pending.operation_id, .turn_id = turn_id};
            }
            case CodexRequestKind::kInterruptTurn: {
              if (!pending.operation_id.has_value()) {
                return absl::InternalError("Codex interrupt request has no operation id");
              }
              return CodexTurnInterrupted{.operation_id = *pending.operation_id};
            }
          }
          return absl::InternalError("unhandled Codex response kind");
        }

        ASSIGN_OR_RETURN(const std::string method,
                         RequiredString(message, "method", "Codex notification"));
        if (method == "item/completed") {
          if (!message.contains("params") || !message.at("params").is_object()) {
            return absl::DataLossError("Codex item completion has no params");
          }
          const nlohmann::json& item = message.at("params").at("item");
          if (!item.is_object()) return absl::DataLossError("Codex item completion has no item");
          ASSIGN_OR_RETURN(const std::string type, RequiredString(item, "type", "Codex item"));
          if (type != "imageGeneration") return CodexIgnoredEvent{};
          ASSIGN_OR_RETURN(CodexProtocolEvent completed, ParseImageCompleted(message.at("params")));
          return completed;
        }
        if (method == "turn/completed") {
          if (!message.contains("params") || !message.at("params").is_object()) {
            return absl::DataLossError("Codex turn completion has no params");
          }
          ASSIGN_OR_RETURN(CodexProtocolEvent completed, ParseTurnCompleted(message.at("params")));
          return completed;
        }
        return CodexIgnoredEvent{};
      });
}

}  // namespace zebes
