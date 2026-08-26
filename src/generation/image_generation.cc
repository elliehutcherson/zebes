#include "generation/image_generation.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {

absl::Status ValidateImageGenerationSpec(const ImageGenerationSpec& spec,
                                         const ImageGenerationCapabilities& capabilities) {
  if (spec.prompt.empty()) return absl::InvalidArgumentError("image prompt must not be empty");
  if (spec.instructions.has_value() && spec.instructions->empty()) {
    return absl::InvalidArgumentError("image instructions must not be empty when supplied");
  }
  if (capabilities.maximum_candidates <= 0) {
    return absl::FailedPreconditionError(
        "image generation provider reported an invalid candidate limit");
  }
  if (spec.requested_candidates <= 0 ||
      spec.requested_candidates > capabilities.maximum_candidates) {
    return absl::InvalidArgumentError(absl::StrCat("requested candidates must be between 1 and ",
                                                   capabilities.maximum_candidates));
  }
  if (spec.target_aspect.width <= 0 || spec.target_aspect.height <= 0) {
    return absl::InvalidArgumentError("image target aspect must be positive");
  }
  if (spec.negative_prompt.has_value()) {
    if (spec.negative_prompt->empty()) {
      return absl::InvalidArgumentError("negative prompt must not be empty when supplied");
    }
    if (!capabilities.supports_negative_prompt) {
      return absl::InvalidArgumentError("image generation provider has no negative prompts");
    }
  }
  if (spec.transparency == ImageTransparencyPreference::kPreferTransparent &&
      !capabilities.supports_transparency) {
    return absl::InvalidArgumentError("image generation provider has no transparent output");
  }
  if (spec.reference_image.has_value()) {
    if (!spec.reference_image->IsValid()) {
      return absl::InvalidArgumentError("image generation reference image is invalid");
    }
    if (!capabilities.supports_reference_image) {
      return absl::InvalidArgumentError("image generation provider has no reference images");
    }
  }
  return absl::OkStatus();
}

std::string ComposeImageGenerationPrompt(const ImageGenerationSpec& spec) {
  if (!spec.instructions.has_value()) return spec.prompt;
  return absl::StrCat(*spec.instructions, "\n\nSubject request:\n", spec.prompt);
}

absl::Status ValidateImageGenerationResult(const ImageGenerationResult& result) {
  if (result.provider.empty() || result.model.empty() || result.submitted_prompt.empty()) {
    return absl::InvalidArgumentError(
        "image generation result needs provider, model, and submitted prompt");
  }
  if (result.provider_request_id.has_value() && result.provider_request_id->empty()) {
    return absl::InvalidArgumentError("provider request ID must not be empty when supplied");
  }
  if (result.candidates.empty()) {
    return absl::InvalidArgumentError("image generation result has no candidates");
  }
  for (const ImageGenerationCandidate& candidate : result.candidates) {
    if (!candidate.image.IsValid()) {
      return absl::InvalidArgumentError("image generation result contains an invalid image");
    }
    if (candidate.revised_prompt.has_value() && candidate.revised_prompt->empty()) {
      return absl::InvalidArgumentError("revised prompt must not be empty when supplied");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<ImageGenerationRequest> ImageGenerationRequest::Create(
    std::unique_ptr<ImageGenerationOperation> operation) {
  if (operation == nullptr) {
    return absl::InvalidArgumentError("image generation request needs an operation");
  }
  return ImageGenerationRequest(std::move(operation));
}

ImageGenerationRequest::~ImageGenerationRequest() { Cancel(); }

ImageGenerationRequest::ImageGenerationRequest(ImageGenerationRequest&& other) noexcept
    : operation_(std::move(other.operation_)), maximum_candidates_(other.maximum_candidates_) {}

ImageGenerationRequest& ImageGenerationRequest::operator=(ImageGenerationRequest&& other) noexcept {
  if (this == &other) return *this;
  Cancel();
  operation_ = std::move(other.operation_);
  maximum_candidates_ = other.maximum_candidates_;
  return *this;
}

absl::StatusOr<std::optional<ImageGenerationResult>> ImageGenerationRequest::Poll() {
  if (operation_ == nullptr) {
    return absl::FailedPreconditionError("image generation request is no longer active");
  }
  bool should_cancel = true;
  absl::Cleanup finish_request = [this, &should_cancel] {
    if (should_cancel) operation_->Cancel();
    operation_.reset();
  };
  ASSIGN_OR_RETURN(std::optional<ImageGenerationResult> result, operation_->Poll());
  if (!result.has_value()) {
    std::move(finish_request).Cancel();
    return std::nullopt;
  }
  const absl::Status valid = ValidateImageGenerationResult(*result);
  if (!valid.ok()) {
    return absl::DataLossError(
        absl::StrCat("image generation provider returned an invalid result: ", valid.message()));
  }
  if (result->candidates.size() > maximum_candidates_) {
    return absl::DataLossError("image generation provider returned too many candidates");
  }
  std::optional<ImageGenerationResult> finished(std::move(*result));
  should_cancel = false;
  return finished;
}

void ImageGenerationRequest::Cancel() noexcept {
  if (operation_ == nullptr) return;
  operation_->Cancel();
  operation_.reset();
}

absl::Duration ImageGenerationRequest::SuggestedPollDelay() const {
  if (operation_ == nullptr) return absl::ZeroDuration();
  return operation_->SuggestedPollDelay();
}

absl::StatusOr<ImageGenerationRequest> ImageGenerationClient::Start(ImageGenerationSpec spec) {
  const ImageGenerationCapabilities capabilities = Capabilities();
  RETURN_IF_ERROR(ValidateImageGenerationSpec(spec, capabilities));
  const size_t maximum_candidates = static_cast<size_t>(spec.requested_candidates);
  ASSIGN_OR_RETURN(ImageGenerationRequest request, StartValidated(std::move(spec)));
  request.maximum_candidates_ = maximum_candidates;
  return request;
}

}  // namespace zebes
