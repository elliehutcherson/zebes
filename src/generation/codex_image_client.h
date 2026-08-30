#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "generation/codex_app_server_transport.h"
#include "generation/image_generation.h"

namespace zebes {

struct CodexImageConfig {
  CodexAppServerProcessConfig process;
  // Trusted provider-owned image cache. When unset, this resolves from
  // CODEX_HOME (or HOME/.codex) to match the App Server child environment.
  std::optional<std::filesystem::path> generated_images_directory;
  std::string model = "gpt-5.6-sol";
  int64_t maximum_candidate_bytes = 64 * 1024 * 1024;
  int64_t maximum_candidate_pixels = 4096 * 4096;
  // Reference pixels are also bounded by the provider-neutral validator
  // before this adapter creates operation-owned PNGs.
  int maximum_reference_images = 8;
  int64_t maximum_reference_pixels = 4096 * 4096;
  absl::Duration request_timeout = absl::Minutes(5);
};

// Subscription-backed image generation through a lazily started local Codex
// App Server. One session multiplexes its operations over one headless stdio
// child. The client uniquely owns that session and its operations borrow it;
// callers must destroy requests before their client, as ImageGenerationEngine
// does through its member ownership order.
//
// The adapter accepts only ChatGPT-authenticated Codex sessions and strips
// OPENAI_API_KEY in the process transport. It grants no approvals, confines
// each ephemeral Codex thread to the transport's private working directory,
// accepts files only from that directory or Codex's generated-image cache, and
// returns only decoded Zebes image types across the provider boundary.
class CodexImageClient final : public ImageGenerationClient {
 public:
  static absl::StatusOr<std::unique_ptr<CodexImageClient>> Create(CodexImageConfig config);

  // Injection seam for deterministic protocol tests. The client owns the
  // transport and starts it lazily just like the process-backed path.
  static absl::StatusOr<std::unique_ptr<CodexImageClient>> CreateWithTransport(
      std::unique_ptr<CodexAppServerTransport> transport, CodexImageConfig config);

  ~CodexImageClient() override;

  ImageGenerationCapabilities Capabilities() const override;

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override;

 private:
  class Session;
  class Operation;

  explicit CodexImageClient(std::unique_ptr<Session> session);

  std::unique_ptr<Session> session_;
};

}  // namespace zebes
