#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "editor/image_generation/codex_app_server_transport.h"
#include "editor/image_generation/image_generation.h"

namespace zebes {

struct CodexImageConfig {
  CodexAppServerProcessConfig process;
  std::string model = "codex-imagegen";
  int64_t maximum_candidate_bytes = 64 * 1024 * 1024;
  int64_t maximum_candidate_pixels = 4096 * 4096;
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
// and returns only decoded Zebes image types across the provider boundary.
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
