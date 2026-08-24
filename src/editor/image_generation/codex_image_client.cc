#include "editor/image_generation/codex_image_client.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "editor/image_generation/codex_app_server_protocol.h"

namespace zebes {
namespace {

constexpr absl::Duration kPollDelay = absl::Milliseconds(50);

struct ImageItem {
  std::filesystem::path saved_path;
  std::optional<std::string> revised_prompt;
};

struct BoundedImageFile {
  std::vector<uint8_t> bytes;
  bool owned_by_zebes = false;
};

struct WaitingForSession {};
struct StartingThread {};

struct StartingTurn {
  std::string thread_id;
};

struct Generating {
  std::string thread_id;
  std::string turn_id;
};

struct AwaitingTurnCompletion {
  std::string thread_id;
  std::string turn_id;
  ImageItem image;
};

struct OperationCompleted {
  absl::StatusOr<ImageGenerationResult> outcome;
};

using OperationPhase = std::variant<WaitingForSession, StartingThread, StartingTurn, Generating,
                                    AwaitingTurnCompletion, OperationCompleted>;

struct OperationState {
  OperationState(uint64_t operation_id, ImageGenerationSpec generation_spec,
                 absl::Time operation_deadline)
      : id(operation_id),
        spec(std::move(generation_spec)),
        deadline(operation_deadline),
        phase(WaitingForSession{}) {}

  uint64_t id;
  ImageGenerationSpec spec;
  absl::Time deadline;
  OperationPhase phase;
};

struct SessionNotStarted {};
struct SessionInitializing {};
struct SessionReadingAccount {};
struct SessionReadingSkills {};

struct SessionReady {
  std::filesystem::path imagegen_skill_path;
};

struct SessionFailed {
  absl::Status status;
};

using SessionState = std::variant<SessionNotStarted, SessionInitializing, SessionReadingAccount,
                                  SessionReadingSkills, SessionReady, SessionFailed>;

struct ActiveTurn {
  std::string_view thread_id;
  std::string_view turn_id;
};

std::optional<ActiveTurn> GetActiveTurn(const OperationState& state) {
  if (const auto* generating = std::get_if<Generating>(&state.phase)) {
    return ActiveTurn{.thread_id = generating->thread_id, .turn_id = generating->turn_id};
  }
  if (const auto* awaiting = std::get_if<AwaitingTurnCompletion>(&state.phase)) {
    return ActiveTurn{.thread_id = awaiting->thread_id, .turn_id = awaiting->turn_id};
  }
  return std::nullopt;
}

bool IsWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
  auto child_component = child.begin();
  for (auto parent_component = parent.begin(); parent_component != parent.end();
       ++parent_component, ++child_component) {
    if (child_component == child.end() || *child_component != *parent_component) return false;
  }
  return true;
}

absl::StatusOr<std::filesystem::path> ResolveGeneratedImagesDirectory(
    const std::optional<std::filesystem::path>& configured) {
  std::filesystem::path directory;
  if (configured.has_value()) {
    directory = *configured;
  } else if (const char* const codex_home = std::getenv("CODEX_HOME");
             codex_home != nullptr && *codex_home != '\0') {
    directory = std::filesystem::path(codex_home) / "generated_images";
  } else if (const char* const home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    directory = std::filesystem::path(home) / ".codex" / "generated_images";
  } else {
    return absl::FailedPreconditionError(
        "Codex image generation requires CODEX_HOME or HOME to locate its image cache");
  }
  if (!directory.is_absolute()) {
    return absl::InvalidArgumentError("Codex generated-images directory must be absolute");
  }
  return directory.lexically_normal();
}

absl::StatusOr<BoundedImageFile> ReadBoundedFile(
    const std::filesystem::path& path, const std::filesystem::path& working_directory,
    const std::filesystem::path& generated_images_directory, int64_t maximum_bytes) {
  if (!path.is_absolute()) {
    return absl::DataLossError("Codex image path is not absolute");
  }
  std::error_code error;
  const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
  if (error) {
    return absl::DataLossError(absl::StrCat("could not inspect Codex image: ", error.message()));
  }
  if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
    return absl::DataLossError("Codex image path is not a regular non-symlink file");
  }

  const std::filesystem::path canonical_working_directory =
      std::filesystem::weakly_canonical(working_directory, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not resolve Codex working directory: ", error.message()));
  }
  const std::filesystem::path canonical_generated_images =
      std::filesystem::weakly_canonical(generated_images_directory, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not resolve Codex generated-images directory: ", error.message()));
  }
  const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error);
  if (error) {
    return absl::DataLossError(
        absl::StrCat("could not resolve Codex image path: ", error.message()));
  }
  const bool owned_by_zebes = IsWithin(canonical_path, canonical_working_directory);
  if (!owned_by_zebes && !IsWithin(canonical_path, canonical_generated_images)) {
    return absl::PermissionDeniedError(
        "Codex image path is outside the private workspace and Codex image cache");
  }

  const uintmax_t size = std::filesystem::file_size(canonical_path, error);
  if (error) {
    return absl::DataLossError(absl::StrCat("could not measure Codex image: ", error.message()));
  }
  if (size == 0 || size > static_cast<uintmax_t>(maximum_bytes)) {
    return absl::ResourceExhaustedError(
        absl::StrCat("Codex image size ", size, " is outside the configured limit"));
  }

  std::ifstream stream(canonical_path, std::ios::binary);
  if (!stream) return absl::DataLossError("could not open Codex image");
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return absl::DataLossError("could not read the complete Codex image");
  }
  return BoundedImageFile{.bytes = std::move(bytes), .owned_by_zebes = owned_by_zebes};
}

absl::Status RequestFailureStatus(CodexRequestKind kind, std::string_view detail) {
  if (kind == CodexRequestKind::kReadAccount) {
    return absl::UnauthenticatedError(absl::StrCat("could not read Codex account: ", detail));
  }
  if (kind == CodexRequestKind::kListSkills) {
    return absl::NotFoundError(absl::StrCat("could not discover Codex imagegen skill: ", detail));
  }
  return absl::UnavailableError(absl::StrCat("Codex App Server request failed: ", detail));
}

}  // namespace

class CodexImageClient::Session {
 public:
  Session(std::unique_ptr<CodexAppServerTransport> transport, CodexImageConfig config,
          std::filesystem::path generated_images_directory)
      : transport_(std::move(transport)),
        config_(std::move(config)),
        generated_images_directory_(std::move(generated_images_directory)) {}

  ~Session() { transport_->Stop(); }

  absl::StatusOr<uint64_t> Start(ImageGenerationSpec spec) {
    if (const auto* failed = std::get_if<SessionFailed>(&session_state_)) {
      return failed->status;
    }
    const absl::Status started = EnsureStarted();
    if (!started.ok()) {
      FailSession(started);
      return started;
    }

    const uint64_t id = next_operation_id_++;
    auto [operation, inserted] =
        operations_.try_emplace(id, id, std::move(spec), absl::Now() + config_.request_timeout);
    if (!inserted) return absl::InternalError("Codex operation id was reused");
    OperationState& state = operation->second;
    if (std::holds_alternative<SessionReady>(session_state_)) {
      const absl::Status status = StartThread(state);
      if (!status.ok()) {
        FailSession(status);
        return status;
      }
    }
    return id;
  }

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll(uint64_t operation_id) {
    const auto found = operations_.find(operation_id);
    if (found == operations_.end()) {
      return absl::FailedPreconditionError("Codex image operation is no longer active");
    }
    OperationState& state = found->second;
    if (!std::holds_alternative<OperationCompleted>(state.phase)) {
      const absl::Status pumped = Pump();
      if (!pumped.ok()) FailSession(pumped);
      const absl::Status expired = ExpireOperations();
      if (!expired.ok()) FailSession(expired);
    }
    auto* completed = std::get_if<OperationCompleted>(&state.phase);
    if (completed == nullptr) return std::nullopt;

    absl::StatusOr<ImageGenerationResult> outcome = std::move(completed->outcome);
    operations_.erase(operation_id);
    ASSIGN_OR_RETURN(ImageGenerationResult result, std::move(outcome));
    return std::optional<ImageGenerationResult>(std::move(result));
  }

  void Cancel(uint64_t operation_id) noexcept {
    const auto found = operations_.find(operation_id);
    if (found == operations_.end()) return;
    OperationState& state = found->second;
    absl::Status interrupt_status = absl::OkStatus();
    if (const std::optional<ActiveTurn> turn = GetActiveTurn(state)) {
      interrupt_status =
          Send(protocol_.InterruptTurn(operation_id, turn->thread_id, turn->turn_id));
    }
    operations_.erase(found);
    if (!interrupt_status.ok()) FailSession(interrupt_status);
  }

 private:
  absl::Status EnsureStarted() {
    if (const auto* failed = std::get_if<SessionFailed>(&session_state_)) {
      return failed->status;
    }
    if (!std::holds_alternative<SessionNotStarted>(session_state_)) return absl::OkStatus();
    RETURN_IF_ERROR(transport_->Start());
    session_state_ = SessionInitializing{};
    return Send(protocol_.Initialize());
  }

  absl::Status Send(absl::StatusOr<std::string> line) {
    ASSIGN_OR_RETURN(std::string encoded, std::move(line));
    return transport_->SendLine(std::move(encoded));
  }

  absl::Status Pump() {
    ASSIGN_OR_RETURN(std::vector<std::string> lines, transport_->Poll());
    for (const std::string& line : lines) {
      ASSIGN_OR_RETURN(CodexProtocolEvent event, protocol_.Parse(line));
      RETURN_IF_ERROR(DispatchEvent(event));
    }
    return absl::OkStatus();
  }

  absl::Status DispatchEvent(const CodexProtocolEvent& event) {
    return std::visit([this](const auto& value) { return HandleEvent(value); }, event);
  }

  static absl::Status HandleEvent(const CodexSessionRequestFailed& failure) {
    return RequestFailureStatus(failure.kind, failure.detail);
  }

  absl::Status HandleEvent(const CodexOperationRequestFailed& failure) {
    if (failure.kind == CodexRequestKind::kInterruptTurn) return absl::OkStatus();
    Complete(failure.operation_id, RequestFailureStatus(failure.kind, failure.detail));
    return absl::OkStatus();
  }

  absl::Status HandleEvent(const CodexInitialized&) {
    if (!std::holds_alternative<SessionInitializing>(session_state_)) {
      return absl::DataLossError("Codex initialize response arrived out of order");
    }
    RETURN_IF_ERROR(Send(protocol_.InitializedNotification()));
    session_state_ = SessionReadingAccount{};
    return Send(protocol_.ReadAccount());
  }

  absl::Status HandleEvent(const CodexAccountRead& account) {
    if (!std::holds_alternative<SessionReadingAccount>(session_state_)) {
      return absl::DataLossError("Codex account response arrived out of order");
    }
    if (!account.type.has_value()) {
      return absl::UnauthenticatedError("Codex has no active account");
    }
    if (*account.type != "chatgpt") {
      return absl::UnauthenticatedError(
          absl::StrCat("Codex must use ChatGPT authentication, not ", *account.type));
    }
    session_state_ = SessionReadingSkills{};
    return Send(protocol_.ListSkills(transport_->working_directory()));
  }

  absl::Status HandleEvent(const CodexSkillsListed& listed) {
    if (!std::holds_alternative<SessionReadingSkills>(session_state_)) {
      return absl::DataLossError("Codex skills response arrived out of order");
    }
    for (const CodexSkillDirectory& directory : listed.directories) {
      if (directory.cwd != transport_->working_directory()) continue;
      for (const CodexSkill& skill : directory.skills) {
        if (skill.name != "imagegen") continue;
        if (!skill.enabled) {
          return absl::FailedPreconditionError("Codex imagegen skill is disabled");
        }
        if (!skill.path.is_absolute()) {
          return absl::DataLossError("Codex imagegen skill path is not absolute");
        }
        session_state_ = SessionReady{.imagegen_skill_path = skill.path};
        return StartWaitingOperations();
      }
    }
    return absl::NotFoundError("Codex has no enabled imagegen skill");
  }

  absl::Status StartWaitingOperations() {
    for (auto& operation : operations_) {
      OperationState& state = operation.second;
      if (!std::holds_alternative<WaitingForSession>(state.phase)) continue;
      RETURN_IF_ERROR(StartThread(state));
    }
    return absl::OkStatus();
  }

  absl::Status StartThread(OperationState& state) {
    if (!std::holds_alternative<WaitingForSession>(state.phase)) {
      return absl::InternalError("Codex operation was not waiting for a thread");
    }
    state.phase = StartingThread{};
    return Send(
        protocol_.StartThread(state.id, transport_->working_directory(), state.spec.instructions));
  }

  absl::Status HandleEvent(const CodexThreadStarted& started) {
    const auto found = operations_.find(started.operation_id);
    if (found == operations_.end()) return absl::OkStatus();
    OperationState& state = found->second;
    if (!std::holds_alternative<StartingThread>(state.phase)) {
      return absl::DataLossError("Codex thread response arrived out of order");
    }
    const auto* ready = std::get_if<SessionReady>(&session_state_);
    if (ready == nullptr) {
      return absl::DataLossError("Codex thread response arrived before the session was ready");
    }

    state.phase = StartingTurn{.thread_id = started.thread_id};
    const std::string prompt =
        absl::StrCat("$imagegen ", state.spec.prompt,
                     "\nGenerate exactly one PNG. Target composition aspect ratio: ",
                     state.spec.target_aspect.width, ":", state.spec.target_aspect.height,
                     ". Do not inspect or edit project files.");
    return Send(
        protocol_.StartTurn(state.id, started.thread_id, prompt, ready->imagegen_skill_path));
  }

  absl::Status HandleEvent(const CodexTurnStarted& started) {
    const auto found = operations_.find(started.operation_id);
    if (found == operations_.end()) return absl::OkStatus();
    OperationState& state = found->second;
    const auto* starting = std::get_if<StartingTurn>(&state.phase);
    if (starting == nullptr) {
      return absl::DataLossError("Codex turn response arrived out of order");
    }
    const std::string thread_id = starting->thread_id;
    state.phase = Generating{.thread_id = thread_id, .turn_id = started.turn_id};
    return absl::OkStatus();
  }

  absl::Status HandleEvent(const CodexImageSucceeded& succeeded) {
    OperationState* const state = FindByTurn(succeeded.turn_id);
    if (state == nullptr) return absl::OkStatus();
    const auto* generating = std::get_if<Generating>(&state->phase);
    if (generating == nullptr) {
      Complete(state->id, absl::DataLossError("Codex returned more than one generated image"));
      return absl::OkStatus();
    }
    const std::string thread_id = generating->thread_id;
    const std::string turn_id = generating->turn_id;
    state->phase = AwaitingTurnCompletion{
        .thread_id = thread_id,
        .turn_id = turn_id,
        .image = {.saved_path = succeeded.saved_path, .revised_prompt = succeeded.revised_prompt},
    };
    return absl::OkStatus();
  }

  absl::Status HandleEvent(const CodexImageFailed& failed) {
    OperationState* const state = FindByTurn(failed.turn_id);
    if (state == nullptr) return absl::OkStatus();
    if (failed.failure == CodexImageFailure::kUsageLimitExceeded) {
      Complete(state->id,
               absl::ResourceExhaustedError("Codex image-generation usage limit was reached"));
      return absl::OkStatus();
    }
    Complete(state->id,
             absl::UnknownError(absl::StrCat("Codex image generation ended as ", failed.status)));
    return absl::OkStatus();
  }

  absl::Status HandleEvent(const CodexTurnSucceeded& succeeded) {
    OperationState* const state = FindByTurn(succeeded.turn_id);
    if (state == nullptr) return absl::OkStatus();
    const auto* awaiting = std::get_if<AwaitingTurnCompletion>(&state->phase);
    if (awaiting == nullptr) {
      Complete(state->id, absl::NotFoundError("Codex turn completed without an image"));
      return absl::OkStatus();
    }
    absl::StatusOr<ImageGenerationResult> result = DecodeResult(*state, *awaiting);
    Complete(state->id, std::move(result));
    return absl::OkStatus();
  }

  absl::Status HandleEvent(const CodexTurnFailed& failed) {
    OperationState* const state = FindByTurn(failed.turn_id);
    if (state == nullptr) return absl::OkStatus();
    Complete(state->id,
             absl::UnavailableError(absl::StrCat("Codex turn ended as ", failed.status)));
    return absl::OkStatus();
  }

  static absl::Status HandleEvent(const CodexTurnInterrupted&) { return absl::OkStatus(); }

  static absl::Status HandleEvent(const CodexServerRequest&) {
    return absl::PermissionDeniedError(
        "Codex App Server requested an approval or dynamic tool from Zebes");
  }

  static absl::Status HandleEvent(const CodexIgnoredEvent&) { return absl::OkStatus(); }

  absl::StatusOr<ImageGenerationResult> DecodeResult(const OperationState& state,
                                                     const AwaitingTurnCompletion& awaiting) {
    const std::filesystem::path path = awaiting.image.saved_path;
    ASSIGN_OR_RETURN(BoundedImageFile file,
                     ReadBoundedFile(path, transport_->working_directory(),
                                     generated_images_directory_, config_.maximum_candidate_bytes));
    absl::Cleanup remove_image = [&file, path] {
      if (!file.owned_by_zebes) return;
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    };
    ASSIGN_OR_RETURN(RgbaImage image, DecodeImage(absl::Span<const uint8_t>(file.bytes),
                                                  config_.maximum_candidate_pixels));
    ImageGenerationResult result{
        .provider = "openai-codex",
        .model = config_.model,
        .submitted_prompt = state.spec.prompt,
        .provider_request_id = awaiting.turn_id,
    };
    result.candidates.push_back(ImageGenerationCandidate{
        .image = std::move(image),
        .revised_prompt = awaiting.image.revised_prompt,
    });
    return result;
  }

  OperationState* FindByTurn(const std::string& turn_id) {
    for (auto& operation : operations_) {
      OperationState& state = operation.second;
      const std::optional<ActiveTurn> active_turn = GetActiveTurn(state);
      if (active_turn.has_value() && active_turn->turn_id == turn_id) return &state;
    }
    return nullptr;
  }

  absl::Status ExpireOperations() {
    const absl::Time now = absl::Now();
    for (auto& [id, state] : operations_) {
      if (std::holds_alternative<OperationCompleted>(state.phase) || now < state.deadline) continue;
      if (const std::optional<ActiveTurn> turn = GetActiveTurn(state)) {
        RETURN_IF_ERROR(Send(protocol_.InterruptTurn(id, turn->thread_id, turn->turn_id)));
      }
      Complete(id, absl::DeadlineExceededError("Codex image generation timed out"));
    }
    return absl::OkStatus();
  }

  void Complete(uint64_t operation_id, absl::StatusOr<ImageGenerationResult> outcome) {
    const auto found = operations_.find(operation_id);
    if (found == operations_.end() ||
        std::holds_alternative<OperationCompleted>(found->second.phase)) {
      return;
    }
    found->second.phase = OperationCompleted{.outcome = std::move(outcome)};
  }

  void FailSession(absl::Status status) {
    if (status.ok()) status = absl::UnknownError("Codex App Server failed without an error");
    if (const auto* failed = std::get_if<SessionFailed>(&session_state_)) {
      status = failed->status;
    } else {
      session_state_ = SessionFailed{.status = status};
    }
    for (auto& operation : operations_) {
      OperationState& state = operation.second;
      if (!std::holds_alternative<OperationCompleted>(state.phase)) {
        state.phase = OperationCompleted{.outcome = status};
      }
    }
    transport_->Stop();
  }

  std::unique_ptr<CodexAppServerTransport> transport_;
  CodexImageConfig config_;
  std::filesystem::path generated_images_directory_;
  CodexAppServerProtocol protocol_;
  SessionState session_state_ = SessionNotStarted{};
  uint64_t next_operation_id_ = 1;
  absl::flat_hash_map<uint64_t, OperationState> operations_;
};

class CodexImageClient::Operation final : public ImageGenerationOperation {
 public:
  Operation(Session& session, uint64_t id) : session_(&session), id_(id) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    return session_->Poll(id_);
  }

  void Cancel() noexcept override { session_->Cancel(id_); }

  absl::Duration SuggestedPollDelay() const override { return kPollDelay; }

 private:
  Session* session_;
  uint64_t id_;
};

absl::StatusOr<std::unique_ptr<CodexImageClient>> CodexImageClient::Create(
    CodexImageConfig config) {
  ASSIGN_OR_RETURN(std::unique_ptr<CodexAppServerTransport> transport,
                   CreateCodexAppServerProcess(config.process));
  return CreateWithTransport(std::move(transport), std::move(config));
}

absl::StatusOr<std::unique_ptr<CodexImageClient>> CodexImageClient::CreateWithTransport(
    std::unique_ptr<CodexAppServerTransport> transport, CodexImageConfig config) {
  if (transport == nullptr) {
    return absl::InvalidArgumentError("Codex image client requires an App Server transport");
  }
  if (config.model.empty()) {
    return absl::InvalidArgumentError("Codex image client requires a model label");
  }
  if (config.maximum_candidate_bytes <= 0 || config.maximum_candidate_pixels <= 0) {
    return absl::InvalidArgumentError("Codex image client byte and pixel limits must be positive");
  }
  if (config.request_timeout <= absl::ZeroDuration() ||
      config.request_timeout == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("Codex image client requires a finite positive timeout");
  }
  ASSIGN_OR_RETURN(const std::filesystem::path generated_images_directory,
                   ResolveGeneratedImagesDirectory(config.generated_images_directory));
  auto session = std::make_unique<Session>(std::move(transport), std::move(config),
                                           generated_images_directory);
  return std::unique_ptr<CodexImageClient>(new CodexImageClient(std::move(session)));
}

CodexImageClient::CodexImageClient(std::unique_ptr<Session> session)
    : session_(std::move(session)) {}

CodexImageClient::~CodexImageClient() = default;

ImageGenerationCapabilities CodexImageClient::Capabilities() const {
  return ImageGenerationCapabilities{
      .maximum_candidates = 1,
      .supports_negative_prompt = false,
      .supports_transparency = false,
      .supports_reference_image = false,
  };
}

absl::StatusOr<ImageGenerationRequest> CodexImageClient::StartValidated(ImageGenerationSpec spec) {
  ASSIGN_OR_RETURN(const uint64_t id, session_->Start(std::move(spec)));
  return ImageGenerationRequest::Create(std::make_unique<Operation>(*session_, id));
}

}  // namespace zebes
