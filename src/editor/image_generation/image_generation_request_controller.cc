#include "editor/image_generation/image_generation_request_controller.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_engine.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ImageGenerationRequestController>>
ImageGenerationRequestController::Create(ImageGenerationProviderRegistry* registry) {
  if (registry == nullptr) {
    return absl::InvalidArgumentError("image generation controller needs a provider registry");
  }
  for (size_t index = 0; index < registry->providers.size(); ++index) {
    const ImageGenerationProvider& provider = registry->providers[index];
    if (provider.name.empty()) {
      return absl::InvalidArgumentError("image generation provider name must not be empty");
    }
    if (provider.engine == nullptr && provider.unavailable_reason.empty()) {
      return absl::InvalidArgumentError(
          "image generation provider without an engine needs an unavailable reason");
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (registry->providers[previous].name == provider.name) {
        return absl::InvalidArgumentError("image generation provider names must be unique");
      }
    }
  }

  auto controller = absl::WrapUnique(new ImageGenerationRequestController(registry));
  const auto available =
      std::find_if(registry->providers.begin(), registry->providers.end(),
                   [](const ImageGenerationProvider& provider) { return provider.available(); });
  if (available != registry->providers.end()) {
    controller->selected_provider_ = static_cast<size_t>(available - registry->providers.begin());
  }
  return controller;
}

ImageGenerationRequestController::~ImageGenerationRequestController() {
  const absl::Status cancelled = Cancel();
  if (!cancelled.ok()) {
    LOG(ERROR) << "Could not cancel image generation during controller shutdown: " << cancelled;
  }
}

ImageGenerationCapabilities ImageGenerationRequestController::capabilities() const {
  if (selected_provider_ >= registry_->providers.size()) return {};
  const ImageGenerationProvider& provider = registry_->providers[selected_provider_];
  return provider.available() ? provider.engine->Capabilities() : ImageGenerationCapabilities{};
}

absl::Status ImageGenerationRequestController::SelectProvider(size_t index) {
  if (index >= registry_->providers.size()) {
    return absl::NotFoundError("the selected image generation provider does not exist");
  }
  if (pending_.has_value()) {
    return absl::FailedPreconditionError("cancel the running generation before changing providers");
  }
  selected_provider_ = index;
  const ImageGenerationProvider& provider = registry_->providers[index];
  if (!provider.available()) return absl::UnavailableError(provider.unavailable_reason);
  return absl::OkStatus();
}

absl::Status ImageGenerationRequestController::Submit(ImageGenerationSpec spec) {
  if (pending_.has_value()) {
    return absl::FailedPreconditionError("an image generation request is already running");
  }
  if (registry_->providers.empty() || selected_provider_ >= registry_->providers.size()) {
    return absl::UnavailableError("no image generation provider is available");
  }
  ImageGenerationProvider& provider = registry_->providers[selected_provider_];
  if (!provider.available()) return absl::UnavailableError(provider.unavailable_reason);
  absl::StatusOr<uint64_t> id = provider.engine->Submit(std::move(spec));
  if (!id.ok()) return id.status();
  pending_ = PendingRequest{.provider = selected_provider_, .id = *id};
  review_.reset();
  return absl::OkStatus();
}

absl::Status ImageGenerationRequestController::Cancel() {
  if (!pending_.has_value()) return absl::OkStatus();
  if (pending_->cancel_requested) return absl::OkStatus();
  PendingRequest& pending = *pending_;
  ImageGenerationEngine* const engine = registry_->providers[pending.provider].engine;
  if (engine == nullptr) {
    return absl::FailedPreconditionError(
        "image generation provider became unavailable while a request was running");
  }
  // Keep the id pending: the engine still owes exactly one event for it.
  const absl::Status cancelled = engine->Cancel(pending.id);
  if (cancelled.ok()) pending.cancel_requested = true;
  return cancelled;
}

bool ImageGenerationRequestController::DisablesProvider(const absl::Status& failure) {
  return failure.code() == absl::StatusCode::kUnauthenticated ||
         failure.code() == absl::StatusCode::kPermissionDenied ||
         failure.code() == absl::StatusCode::kNotFound ||
         failure.code() == absl::StatusCode::kFailedPrecondition ||
         failure.code() == absl::StatusCode::kUnimplemented;
}

absl::StatusOr<bool> ImageGenerationRequestController::Poll() {
  if (!pending_.has_value()) return false;
  const PendingRequest pending = *pending_;
  ImageGenerationProvider& provider = registry_->providers[pending.provider];
  if (provider.engine == nullptr) {
    pending_.reset();
    return absl::FailedPreconditionError(
        "image generation provider became unavailable while a request was running");
  }
  std::optional<GenerationEvent> event = provider.engine->NextEvent(pending.id);
  if (!event.has_value()) return false;
  pending_.reset();
  if (!event->result.ok()) {
    const absl::Status failure = event->result.status();
    if (provider.disable_after_failure || DisablesProvider(failure)) {
      provider.unavailable_reason = std::string(failure.message());
    }
    return failure;
  }
  ImageGenerationResult result = *std::move(event->result);
  review_ = ImageGenerationReview{
      .provider = std::move(result.provider),
      .model = std::move(result.model),
      .submitted_prompt = std::move(result.submitted_prompt),
      .provider_request_id = std::move(result.provider_request_id),
      .generated_at_utc = absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", absl::Now(), absl::UTCTimeZone()),
      .candidates = std::move(result.candidates),
  };
  return true;
}

const ImageGenerationCandidate* ImageGenerationRequestController::SelectedCandidate() const {
  if (!review_.has_value()) return nullptr;
  return &review_->candidates[review_->selected];
}

void ImageGenerationRequestController::SelectCandidate(size_t index) {
  if (!review_.has_value() || index >= review_->candidates.size()) return;
  review_->selected = index;
}

absl::Status ImageGenerationRequestController::AcceptCandidate(
    const std::function<absl::Status(const ImageGenerationReview&,
                                     const ImageGenerationCandidate&)>& accept) {
  if (!review_.has_value()) {
    return absl::FailedPreconditionError("generate a candidate before accepting one");
  }
  const absl::Status accepted = accept(*review_, review_->candidates[review_->selected]);
  if (accepted.ok()) review_.reset();
  return accepted;
}

}  // namespace zebes
