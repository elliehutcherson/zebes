#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace zebes {

enum class CodexRequestKind : uint8_t {
  kInitialize = 0,
  kReadAccount,
  kListModels,
  kListSkills,
  kStartThread,
  kStartTurn,
  kInterruptTurn,
};

struct CodexInitialized {};

struct CodexAccountRead {
  std::optional<std::string> type;
};

struct CodexModelsListed {
  std::vector<std::string> models;
  std::optional<std::string> next_cursor;
};

struct CodexSkill {
  std::string name;
  std::filesystem::path path;
  bool enabled = false;
};

struct CodexSkillDirectory {
  std::filesystem::path cwd;
  std::vector<CodexSkill> skills;
};

struct CodexSkillsListed {
  std::vector<CodexSkillDirectory> directories;
};

struct CodexThreadStarted {
  uint64_t operation_id;
  std::string thread_id;
};

struct CodexTurnStarted {
  uint64_t operation_id;
  std::string turn_id;
};

struct CodexTurnInterrupted {
  uint64_t operation_id;
};

enum class CodexImageFailure : uint8_t {
  kUsageLimitExceeded = 0,
  kOther,
};

struct CodexImageSucceeded {
  std::string turn_id;
  std::filesystem::path saved_path;
  std::optional<std::string> revised_prompt;
};

struct CodexImageFailed {
  std::string turn_id;
  std::string status;
  CodexImageFailure failure = CodexImageFailure::kOther;
};

struct CodexTurnSucceeded {
  std::string turn_id;
};

struct CodexTurnFailed {
  std::string turn_id;
  std::string status;
  std::optional<std::string> detail;
};

struct CodexSessionRequestFailed {
  CodexRequestKind kind;
  std::string detail;
};

struct CodexOperationRequestFailed {
  CodexRequestKind kind;
  uint64_t operation_id;
  std::string detail;
};

struct CodexServerRequest {
  std::string method;
};

struct CodexIgnoredEvent {};

using CodexProtocolEvent =
    std::variant<CodexInitialized, CodexAccountRead, CodexModelsListed, CodexSkillsListed,
                 CodexThreadStarted, CodexTurnStarted, CodexTurnInterrupted, CodexImageSucceeded,
                 CodexImageFailed, CodexTurnSucceeded, CodexTurnFailed, CodexSessionRequestFailed,
                 CodexOperationRequestFailed, CodexServerRequest, CodexIgnoredEvent>;

// The only JSON translation boundary for the Codex App Server protocol.
// Outbound methods return one JSONL payload without a newline. Parse validates
// one inbound payload and returns a typed event; provider code never receives
// a JSON value or catches a JSON exception.
//
// The protocol also owns request IDs and their expected response models. A
// response with an unknown ID is invalid rather than being guessed from its
// shape.
class CodexAppServerProtocol {
 public:
  absl::StatusOr<std::string> Initialize();
  static absl::StatusOr<std::string> InitializedNotification();
  absl::StatusOr<std::string> ReadAccount();
  absl::StatusOr<std::string> ListModels(const std::optional<std::string>& cursor);
  absl::StatusOr<std::string> ListSkills(const std::filesystem::path& cwd);
  absl::StatusOr<std::string> StartThread(
      uint64_t operation_id, const std::filesystem::path& cwd, std::string_view model,
      const std::optional<std::string>& generation_instructions);
  absl::StatusOr<std::string> StartTurn(uint64_t operation_id, std::string_view thread_id,
                                        std::string_view prompt,
                                        const std::filesystem::path& skill_path,
                                        const std::vector<std::filesystem::path>& local_images);
  absl::StatusOr<std::string> InterruptTurn(uint64_t operation_id, std::string_view thread_id,
                                            std::string_view turn_id);

  absl::StatusOr<CodexProtocolEvent> Parse(std::string_view line);

 private:
  struct PendingRequest {
    CodexRequestKind kind;
    std::optional<uint64_t> operation_id;
  };

  absl::StatusOr<std::string> EncodeSessionRequest(CodexRequestKind kind, std::string_view method,
                                                   std::string params_json);
  absl::StatusOr<std::string> EncodeOperationRequest(CodexRequestKind kind, uint64_t operation_id,
                                                     std::string_view method,
                                                     std::string params_json);
  absl::StatusOr<std::string> EncodeRequest(PendingRequest pending, std::string_view method,
                                            std::string params_json);

  int64_t next_request_id_ = 1;
  absl::flat_hash_map<int64_t, PendingRequest> pending_;
};

}  // namespace zebes
