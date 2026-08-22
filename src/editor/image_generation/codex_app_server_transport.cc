#include "editor/image_generation/codex_app_server_transport.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace zebes {
namespace {

#if !defined(_WIN32)

void CloseDescriptor(int& descriptor) noexcept {
  if (descriptor < 0) return;
  close(descriptor);
  descriptor = -1;
}

absl::Status ErrnoStatus(std::string_view action, int error_number) {
  const std::string message =
      absl::StrCat(action, ": ", std::strerror(error_number), " (", error_number, ")");
  if (error_number == ENOENT) return absl::NotFoundError(message);
  if (error_number == EACCES) return absl::PermissionDeniedError(message);
  return absl::InternalError(message);
}

absl::Status SetDescriptorFlags(int descriptor) {
  const int descriptor_flags = fcntl(descriptor, F_GETFD);
  if (descriptor_flags < 0) return ErrnoStatus("could not read descriptor flags", errno);
  if (fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
    return ErrnoStatus("could not set close-on-exec", errno);
  }
  return absl::OkStatus();
}

absl::Status SetNonBlocking(int descriptor) {
  const int status_flags = fcntl(descriptor, F_GETFL);
  if (status_flags < 0) return ErrnoStatus("could not read descriptor status", errno);
  if (fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) < 0) {
    return ErrnoStatus("could not make descriptor non-blocking", errno);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::filesystem::path> CreatePrivateWorkingDirectory() {
  std::error_code error;
  const std::filesystem::path temporary = std::filesystem::temp_directory_path(error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not find the temporary directory: ", error.message()));
  }

  std::string pattern = (temporary / "zebes-codex-imagegen-XXXXXX").string();
  std::vector<char> writable(pattern.begin(), pattern.end());
  writable.push_back('\0');
  char* const created = mkdtemp(writable.data());
  if (created == nullptr) return ErrnoStatus("could not create Codex working directory", errno);
  return std::filesystem::path(created);
}

std::vector<std::string> ChildEnvironment() {
  std::vector<std::string> environment;
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    const std::string_view value(*entry);
    if (value.starts_with("OPENAI_API_KEY=")) continue;
    environment.emplace_back(value);
  }
  return environment;
}

std::vector<char*> MutablePointers(std::vector<std::string>& values) {
  std::vector<char*> pointers;
  pointers.reserve(values.size() + 1);
  for (std::string& value : values) pointers.push_back(value.data());
  pointers.push_back(nullptr);
  return pointers;
}

bool IsExecutableFile(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error) && !error && access(path.c_str(), X_OK) == 0;
}

absl::StatusOr<std::string> CheckedExecutable(const std::filesystem::path& path,
                                              std::string_view source) {
  if (!IsExecutableFile(path)) {
    return absl::NotFoundError(absl::StrCat("Codex executable from ", source,
                                            " is not an executable file: ", path.string()));
  }
  std::error_code error;
  const std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("could not resolve Codex executable path: ", error.message()));
  }
  return absolute.string();
}

std::optional<std::filesystem::path> FindExecutableOnPath(std::string_view executable) {
  const char* const path_value = std::getenv("PATH");
  if (path_value == nullptr) return std::nullopt;
  std::string_view remaining(path_value);
  while (true) {
    const size_t separator = remaining.find(':');
    const std::string_view entry = remaining.substr(0, separator);
    const std::filesystem::path directory =
        entry.empty() ? std::filesystem::current_path() : std::filesystem::path(entry);
    const std::filesystem::path candidate = directory / executable;
    if (IsExecutableFile(candidate)) return candidate;
    if (separator == std::string_view::npos) return std::nullopt;
    remaining.remove_prefix(separator + 1);
  }
}

#if defined(__APPLE__)
std::optional<std::filesystem::path> FindCodexInEditorExtensions(
    const std::filesystem::path& extensions_root) {
  std::error_code error;
  std::filesystem::directory_iterator entries(extensions_root, error);
  if (error) return std::nullopt;

  std::optional<std::filesystem::path> newest;
  std::filesystem::file_time_type newest_time{};
  for (const std::filesystem::directory_entry& entry : entries) {
    if (!entry.is_directory(error) || error) {
      error.clear();
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (!std::string_view(name).starts_with("openai.chatgpt-")) continue;
    for (const char* architecture : {"macos-arm64", "macos-x86_64"}) {
      const std::filesystem::path candidate = entry.path() / "bin" / architecture / "codex";
      if (!IsExecutableFile(candidate)) continue;
      const std::filesystem::file_time_type modified = entry.last_write_time(error);
      if (error) {
        error.clear();
        continue;
      }
      if (!newest.has_value() || modified > newest_time) {
        newest = candidate;
        newest_time = modified;
      }
    }
  }
  return newest;
}

std::optional<std::filesystem::path> FindBundledMacCodex() {
  for (const std::filesystem::path& candidate : {
           std::filesystem::path("/Applications/Codex.app/Contents/Resources/codex"),
           std::filesystem::path("/Applications/ChatGPT.app/Contents/Resources/codex"),
       }) {
    if (IsExecutableFile(candidate)) return candidate;
  }

  const char* const home_value = std::getenv("HOME");
  if (home_value == nullptr || *home_value == '\0') return std::nullopt;
  const std::filesystem::path home(home_value);
  for (const std::filesystem::path& root : {
           home / ".vscode" / "extensions",
           home / ".vscode-insiders" / "extensions",
           home / ".cursor" / "extensions",
       }) {
    if (std::optional<std::filesystem::path> found = FindCodexInEditorExtensions(root);
        found.has_value()) {
      return found;
    }
  }
  return std::nullopt;
}
#endif

absl::StatusOr<std::string> ResolveCodexExecutable(std::string configured) {
  if (configured.empty()) {
    return absl::InvalidArgumentError("Codex App Server executable must not be empty");
  }
  if (configured == "codex") {
    if (const char* const override_value = std::getenv("ZEBES_CODEX_BIN");
        override_value != nullptr && *override_value != '\0') {
      return CheckedExecutable(override_value, "ZEBES_CODEX_BIN");
    }
  }
  if (configured.find('/') != std::string::npos) {
    return CheckedExecutable(configured, "configuration");
  }
  if (std::optional<std::filesystem::path> found = FindExecutableOnPath(configured);
      found.has_value()) {
    return CheckedExecutable(*found, "PATH");
  }
#if defined(__APPLE__)
  if (configured == "codex") {
    if (std::optional<std::filesystem::path> found = FindBundledMacCodex(); found.has_value()) {
      return CheckedExecutable(*found, "an installed OpenAI editor extension");
    }
  }
#endif
  return absl::NotFoundError(absl::StrCat(
      "could not find the Codex executable '", configured,
      "'; install Codex, add it to PATH, or set ZEBES_CODEX_BIN to its absolute path"));
}

class OwnedDescriptor {
 public:
  explicit OwnedDescriptor(int descriptor) : descriptor_(descriptor) {}
  ~OwnedDescriptor() { Reset(); }

  OwnedDescriptor(OwnedDescriptor&& other) noexcept
      : descriptor_(std::exchange(other.descriptor_, -1)) {}

  OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    descriptor_ = std::exchange(other.descriptor_, -1);
    return *this;
  }

  OwnedDescriptor(const OwnedDescriptor&) = delete;
  OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;

  int get() const { return descriptor_; }

  void Reset() noexcept { CloseDescriptor(descriptor_); }

 private:
  int descriptor_;
};

struct TransportCreated {};

struct RunningProcess {
  std::optional<pid_t> child_pid;
  pid_t process_group;
  OwnedDescriptor input;
  OwnedDescriptor output;
  OwnedDescriptor error;
  bool output_eof = false;
};

struct TransportStopped {};

using TransportState = std::variant<TransportCreated, RunningProcess, TransportStopped>;

class PosixCodexAppServerTransport final : public CodexAppServerTransport {
 public:
  PosixCodexAppServerTransport(CodexAppServerProcessConfig config,
                               std::filesystem::path working_directory)
      : config_(std::move(config)), working_directory_(std::move(working_directory)) {}

  ~PosixCodexAppServerTransport() override {
    Stop();
    std::error_code error;
    std::filesystem::remove_all(working_directory_, error);
  }

  absl::Status Start() override {
    if (std::holds_alternative<RunningProcess>(state_)) {
      return absl::FailedPreconditionError("Codex App Server is already started");
    }
    if (std::holds_alternative<TransportStopped>(state_)) {
      return absl::FailedPreconditionError("Codex App Server transport was stopped");
    }

    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    int error_pipe[2] = {-1, -1};
    absl::Cleanup close_pipes = [&] {
      for (int* descriptor : {&input_pipe[0], &input_pipe[1], &output_pipe[0], &output_pipe[1],
                              &error_pipe[0], &error_pipe[1]}) {
        CloseDescriptor(*descriptor);
      }
    };
    if (pipe(input_pipe) < 0 || pipe(output_pipe) < 0 || pipe(error_pipe) < 0) {
      return ErrnoStatus("could not create Codex App Server pipes", errno);
    }
    for (const int descriptor : {input_pipe[0], input_pipe[1], output_pipe[0], output_pipe[1],
                                 error_pipe[0], error_pipe[1]}) {
      const absl::Status status = SetDescriptorFlags(descriptor);
      if (!status.ok()) return status;
    }

    posix_spawn_file_actions_t actions;
    int spawn_error = posix_spawn_file_actions_init(&actions);
    if (spawn_error != 0) {
      return ErrnoStatus("could not initialize Codex spawn actions", spawn_error);
    }
    absl::Cleanup destroy_actions = [&actions] { posix_spawn_file_actions_destroy(&actions); };
    spawn_error = posix_spawn_file_actions_adddup2(&actions, input_pipe[0], STDIN_FILENO);
    if (spawn_error == 0) {
      spawn_error = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    }
    if (spawn_error == 0) {
      spawn_error = posix_spawn_file_actions_adddup2(&actions, error_pipe[1], STDERR_FILENO);
    }
    for (const int descriptor : {input_pipe[0], input_pipe[1], output_pipe[0], output_pipe[1],
                                 error_pipe[0], error_pipe[1]}) {
      if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_addclose(&actions, descriptor);
      }
    }
    if (spawn_error != 0) {
      return ErrnoStatus("could not configure Codex spawn actions", spawn_error);
    }

    posix_spawnattr_t attributes;
    spawn_error = posix_spawnattr_init(&attributes);
    if (spawn_error != 0) {
      return ErrnoStatus("could not initialize Codex spawn attributes", spawn_error);
    }
    absl::Cleanup destroy_attributes = [&attributes] { posix_spawnattr_destroy(&attributes); };
    spawn_error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (spawn_error == 0) spawn_error = posix_spawnattr_setpgroup(&attributes, 0);
    if (spawn_error != 0) {
      return ErrnoStatus("could not configure Codex spawn attributes", spawn_error);
    }

    std::vector<std::string> arguments{config_.executable, "app-server", "--stdio"};
    std::vector<char*> argument_pointers = MutablePointers(arguments);
    std::vector<std::string> environment = ChildEnvironment();
    std::vector<char*> environment_pointers = MutablePointers(environment);

    pid_t child_pid = -1;
    spawn_error = posix_spawnp(&child_pid, config_.executable.c_str(), &actions, &attributes,
                               argument_pointers.data(), environment_pointers.data());
    if (spawn_error != 0) {
      return ErrnoStatus("could not start Codex App Server", spawn_error);
    }

    state_ = RunningProcess{
        .child_pid = child_pid,
        .process_group = child_pid,
        .input = OwnedDescriptor(std::exchange(input_pipe[1], -1)),
        .output = OwnedDescriptor(std::exchange(output_pipe[0], -1)),
        .error = OwnedDescriptor(std::exchange(error_pipe[0], -1)),
    };
    const RunningProcess& running = std::get<RunningProcess>(state_);
    const absl::Status input_status = SetNonBlocking(running.input.get());
    const absl::Status output_status = SetNonBlocking(running.output.get());
    const absl::Status error_status = SetNonBlocking(running.error.get());
    if (input_status.ok() && output_status.ok() && error_status.ok()) return absl::OkStatus();
    Stop();
    if (!input_status.ok()) return input_status;
    if (!output_status.ok()) return output_status;
    return error_status;
  }

  absl::Status SendLine(std::string line) override {
    if (!std::holds_alternative<RunningProcess>(state_)) {
      return absl::FailedPreconditionError("Codex App Server is not running");
    }
    if (line.find_first_of("\r\n") != std::string::npos) {
      return absl::InvalidArgumentError("Codex protocol message contains a raw newline");
    }
    if (pending_write_.size() + line.size() + 1 > config_.maximum_pending_write_bytes) {
      return absl::ResourceExhaustedError("Codex protocol write queue exceeded its limit");
    }
    pending_write_.append(line);
    pending_write_.push_back('\n');
    return FlushWrites();
  }

  absl::StatusOr<std::vector<std::string>> Poll() override {
    RunningProcess* const running = std::get_if<RunningProcess>(&state_);
    if (running == nullptr) {
      return absl::FailedPreconditionError("Codex App Server is not running");
    }
    RETURN_IF_ERROR(FlushWrites());
    RETURN_IF_ERROR(DrainError());
    RETURN_IF_ERROR(DrainOutput());
    ReapWithoutWaiting();

    std::vector<std::string> lines;
    size_t consumed = 0;
    while (true) {
      const size_t newline = output_buffer_.find('\n', consumed);
      if (newline == std::string::npos) break;
      const size_t length = newline - consumed;
      if (length > config_.maximum_protocol_line_bytes) {
        return absl::DataLossError("Codex App Server emitted an oversized protocol line");
      }
      std::string line = output_buffer_.substr(consumed, length);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines.push_back(std::move(line));
      consumed = newline + 1;
    }
    if (consumed > 0) output_buffer_.erase(0, consumed);
    if (output_buffer_.size() > config_.maximum_protocol_line_bytes) {
      return absl::DataLossError("Codex App Server emitted an oversized protocol line");
    }
    if (running->output_eof && !output_buffer_.empty()) {
      return absl::DataLossError("Codex App Server ended with an incomplete protocol line");
    }
    if (!lines.empty()) return lines;
    if (!running->child_pid.has_value() || running->output_eof) return ProcessEndedStatus();
    return lines;
  }

  void Stop() noexcept override {
    if (std::holds_alternative<TransportStopped>(state_)) return;
    RunningProcess* const running = std::get_if<RunningProcess>(&state_);
    if (running == nullptr) {
      state_ = TransportStopped{};
      return;
    }

    running->input.Reset();
    if (running->child_pid.has_value()) {
      kill(-running->process_group, SIGTERM);
      for (int attempt = 0; attempt < 10 && running->child_pid.has_value(); ++attempt) {
        ReapWithoutWaiting();
        if (!running->child_pid.has_value()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (running->child_pid.has_value()) {
        kill(-running->process_group, SIGKILL);
        int status = 0;
        const pid_t child_pid = *running->child_pid;
        while (waitpid(child_pid, &status, 0) < 0 && errno == EINTR) {
        }
        RecordExitStatus(status);
        running->child_pid.reset();
      }
    }
    state_ = TransportStopped{};
  }

  const std::filesystem::path& working_directory() const override { return working_directory_; }

  std::string diagnostics() const override { return diagnostics_; }

 private:
  absl::Status FlushWrites() {
    RunningProcess* const running = std::get_if<RunningProcess>(&state_);
    if (running == nullptr) return absl::FailedPreconditionError("Codex App Server is not running");
    while (!pending_write_.empty()) {
      const ssize_t written =
          write(running->input.get(), pending_write_.data(), pending_write_.size());
      if (written > 0) {
        pending_write_.erase(0, static_cast<size_t>(written));
        continue;
      }
      if (written < 0 && errno == EINTR) continue;
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return absl::OkStatus();
      }
      return ErrnoStatus("could not write to Codex App Server", errno);
    }
    return absl::OkStatus();
  }

  absl::Status DrainOutput() {
    RunningProcess* const running = std::get_if<RunningProcess>(&state_);
    if (running == nullptr) return absl::FailedPreconditionError("Codex App Server is not running");
    char buffer[64 * 1024];
    while (true) {
      const ssize_t read_size = read(running->output.get(), buffer, sizeof(buffer));
      if (read_size > 0) {
        output_buffer_.append(buffer, static_cast<size_t>(read_size));
        if (output_buffer_.size() > config_.maximum_protocol_line_bytes + 1) {
          return absl::DataLossError("Codex App Server emitted an oversized protocol line");
        }
        continue;
      }
      if (read_size == 0) {
        running->output_eof = true;
        return absl::OkStatus();
      }
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return absl::OkStatus();
      return ErrnoStatus("could not read from Codex App Server", errno);
    }
  }

  absl::Status DrainError() {
    RunningProcess* const running = std::get_if<RunningProcess>(&state_);
    if (running == nullptr) return absl::FailedPreconditionError("Codex App Server is not running");
    char buffer[4096];
    while (true) {
      const ssize_t read_size = read(running->error.get(), buffer, sizeof(buffer));
      if (read_size > 0) {
        diagnostics_.append(buffer, static_cast<size_t>(read_size));
        if (diagnostics_.size() > config_.maximum_diagnostic_bytes) {
          diagnostics_.erase(0, diagnostics_.size() - config_.maximum_diagnostic_bytes);
        }
        continue;
      }
      if (read_size == 0) return absl::OkStatus();
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return absl::OkStatus();
      return ErrnoStatus("could not read Codex App Server diagnostics", errno);
    }
  }

  void ReapWithoutWaiting() noexcept {
    RunningProcess* const running = std::get_if<RunningProcess>(&state_);
    if (running == nullptr || !running->child_pid.has_value()) return;
    int status = 0;
    const pid_t child_pid = *running->child_pid;
    const pid_t reaped = waitpid(child_pid, &status, WNOHANG);
    if (reaped != child_pid) return;
    RecordExitStatus(status);
    running->child_pid.reset();
  }

  void RecordExitStatus(int status) noexcept {
    if (WIFEXITED(status)) {
      exit_description_ = absl::StrCat("exit status ", WEXITSTATUS(status));
      return;
    }
    if (WIFSIGNALED(status)) {
      exit_description_ = absl::StrCat("signal ", WTERMSIG(status));
      return;
    }
    exit_description_ = "unknown status";
  }

  absl::Status ProcessEndedStatus() const {
    std::string message = absl::StrCat("Codex App Server ended with ", exit_description_);
    if (!diagnostics_.empty()) absl::StrAppend(&message, ": ", diagnostics_);
    return absl::UnavailableError(message);
  }

  CodexAppServerProcessConfig config_;
  std::filesystem::path working_directory_;
  TransportState state_ = TransportCreated{};
  std::string pending_write_;
  std::string output_buffer_;
  std::string diagnostics_;
  std::string exit_description_ = "unknown status";
};

#endif

}  // namespace

absl::StatusOr<std::unique_ptr<CodexAppServerTransport>> CreateCodexAppServerProcess(
    CodexAppServerProcessConfig config) {
  if (config.executable.empty()) {
    return absl::InvalidArgumentError("Codex App Server executable must not be empty");
  }
  if (config.maximum_protocol_line_bytes == 0 || config.maximum_pending_write_bytes == 0 ||
      config.maximum_diagnostic_bytes == 0) {
    return absl::InvalidArgumentError("Codex App Server byte limits must be positive");
  }
#if defined(_WIN32)
  return absl::UnimplementedError(
      "Codex App Server process transport is not implemented on Windows");
#else
  ASSIGN_OR_RETURN(config.executable, ResolveCodexExecutable(std::move(config.executable)));
  ASSIGN_OR_RETURN(std::filesystem::path working_directory, CreatePrivateWorkingDirectory());
  return std::make_unique<PosixCodexAppServerTransport>(std::move(config),
                                                        std::move(working_directory));
#endif
}

}  // namespace zebes
