#pragma once

#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "common/blocking_callback_thread.h"
#include "common/engine_runner.h"
#include "editor/image_generation/codex_image_client.h"
#include "editor/image_generation/credential_source.h"
#include "editor/image_generation/http_transport.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_engine.h"
#include "editor/image_generation/openai_image_client.h"

namespace zebes {

// The session-lifetime owner of the image-generation stack: the HTTPS
// transport, the credential source, the provider adapter, the engine holding
// every in-flight request, and the thread that polls it.
//
// One of these belongs in the editor's composition root. Construction starts
// the engine thread and destruction stops and joins it, so a caller only ever
// holds `engine()` and submits to it. The engine is the only part callers
// touch; nothing else here is reachable, because nothing else is theirs to
// order correctly.
//
// Member order is the whole point of this class. The engine owns the client,
// which borrows the transport and the credential source, and the runner
// borrows the engine, so each must outlive the thing that borrows it.
class ImageGenerationService {
 public:
  // Builds the OpenAI stack. `config` names the credential's environment
  // variable, never the credential; a missing key surfaces later as one
  // request's Unauthenticated rather than a startup failure.
  static absl::StatusOr<std::unique_ptr<ImageGenerationService>> CreateOpenAi(
      OpenAiImageConfig config);

  // Builds the subscription-backed Codex stack. The App Server process starts
  // lazily with the first request, uses the active ChatGPT login, and is owned
  // transitively by the engine's client.
  static absl::StatusOr<std::unique_ptr<ImageGenerationService>> CreateCodex(
      CodexImageConfig config);

  // Runs `client` with no transport or credentials of its own. This is the
  // seam for a fake provider; the OpenAI path goes through CreateOpenAi.
  static absl::StatusOr<std::unique_ptr<ImageGenerationService>> Create(
      std::unique_ptr<ImageGenerationClient> client);

  // Stops the runner and joins its thread. A non-OK run status is logged
  // rather than reported, because a destructor has nowhere to report it and
  // the process is already leaving.
  ~ImageGenerationService();

  ImageGenerationService(const ImageGenerationService&) = delete;
  ImageGenerationService& operator=(const ImageGenerationService&) = delete;

  ImageGenerationEngine& engine() { return *engine_; }

 private:
  // `transport` and `credentials` are null when the caller supplied its own
  // client; they exist only to keep the OpenAI adapter's borrows alive.
  ImageGenerationService(std::unique_ptr<HttpTransport> transport,
                         std::unique_ptr<CredentialSource> credentials,
                         std::unique_ptr<ImageGenerationEngine> engine,
                         std::unique_ptr<EngineRunner> runner, BlockingCallbackThread thread);

  std::unique_ptr<HttpTransport> transport_;
  std::unique_ptr<CredentialSource> credentials_;
  std::unique_ptr<ImageGenerationEngine> engine_;
  std::unique_ptr<EngineRunner> runner_;
  // Declared last so it is joined before anything its callback touches is
  // destroyed. The destructor stops the runner first; without that the join
  // would never return.
  std::optional<BlockingCallbackThread> thread_;
};

}  // namespace zebes
