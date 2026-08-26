#include "generation/image_generation_service.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/blocking_callback_thread.h"
#include "common/engine_runner.h"
#include "common/status_macros.h"
#include "generation/codex_image_client.h"
#include "generation/credential_source.h"
#include "generation/curl_http_transport.h"
#include "generation/http_transport.h"
#include "generation/image_generation.h"
#include "generation/image_generation_engine.h"
#include "generation/openai_image_client.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ImageGenerationService>> ImageGenerationService::CreateCodex(
    CodexImageConfig config) {
  ASSIGN_OR_RETURN(std::unique_ptr<CodexImageClient> client,
                   CodexImageClient::Create(std::move(config)));
  return Create(std::move(client));
}

absl::StatusOr<std::unique_ptr<ImageGenerationService>> ImageGenerationService::CreateOpenAi(
    OpenAiImageConfig config) {
  ASSIGN_OR_RETURN(std::unique_ptr<CurlHttpTransport> transport, CurlHttpTransport::Create());
  auto credentials = std::make_unique<EnvironmentCredentialSource>();
  ASSIGN_OR_RETURN(std::unique_ptr<OpenAiImageClient> client,
                   OpenAiImageClient::Create(*transport, *credentials, std::move(config)));

  ASSIGN_OR_RETURN(std::unique_ptr<ImageGenerationEngine> engine,
                   ImageGenerationEngine::Create(std::move(client)));
  ASSIGN_OR_RETURN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSIGN_OR_RETURN(
      BlockingCallbackThread thread,
      BlockingCallbackThread::Start([runner = runner.get()] { return runner->Run(); }));
  return absl::WrapUnique(new ImageGenerationService(std::move(transport), std::move(credentials),
                                                     std::move(engine), std::move(runner),
                                                     std::move(thread)));
}

absl::StatusOr<std::unique_ptr<ImageGenerationService>> ImageGenerationService::Create(
    std::unique_ptr<ImageGenerationClient> client) {
  if (client == nullptr) {
    return absl::InvalidArgumentError("Image generation service requires a client");
  }
  ASSIGN_OR_RETURN(std::unique_ptr<ImageGenerationEngine> engine,
                   ImageGenerationEngine::Create(std::move(client)));
  ASSIGN_OR_RETURN(std::unique_ptr<EngineRunner> runner, EngineRunner::Create(*engine));
  ASSIGN_OR_RETURN(
      BlockingCallbackThread thread,
      BlockingCallbackThread::Start([runner = runner.get()] { return runner->Run(); }));
  return absl::WrapUnique(new ImageGenerationService(/*transport=*/nullptr, /*credentials=*/nullptr,
                                                     std::move(engine), std::move(runner),
                                                     std::move(thread)));
}

ImageGenerationService::ImageGenerationService(std::unique_ptr<HttpTransport> transport,
                                               std::unique_ptr<CredentialSource> credentials,
                                               std::unique_ptr<ImageGenerationEngine> engine,
                                               std::unique_ptr<EngineRunner> runner,
                                               BlockingCallbackThread thread)
    : transport_(std::move(transport)),
      credentials_(std::move(credentials)),
      engine_(std::move(engine)),
      runner_(std::move(runner)),
      thread_(std::move(thread)) {}

ImageGenerationService::~ImageGenerationService() {
  runner_->Stop();
  const absl::Status stopped = thread_->Wait();
  if (stopped.ok()) return;
  LOG(ERROR) << "Image generation engine stopped with an error: " << stopped;
}

}  // namespace zebes
