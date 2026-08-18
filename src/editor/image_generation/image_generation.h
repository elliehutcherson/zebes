#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/image_io.h"

namespace zebes {

struct ImageAspectRatio {
  int width = 1;
  int height = 1;
};

enum class ImageTransparencyPreference : uint8_t {
  kNoPreference = 0,
  kPreferTransparent = 1,
};

struct ImageGenerationSpec {
  std::string prompt;
  std::optional<std::string> negative_prompt;
  int requested_candidates = 1;
  ImageAspectRatio target_aspect;
  ImageTransparencyPreference transparency = ImageTransparencyPreference::kNoPreference;
  std::optional<RgbaImage> reference_image;
};

struct ImageGenerationCapabilities {
  int maximum_candidates = 1;
  bool supports_negative_prompt = false;
  bool supports_transparency = false;
  bool supports_reference_image = false;
};

struct ImageGenerationCandidate {
  RgbaImage image;
  std::optional<std::string> revised_prompt;
};

// Only the stable, provider-neutral provenance needed when a candidate is
// accepted crosses the adapter boundary. Raw provider response objects do not.
struct ImageGenerationResult {
  std::string provider;
  std::string model;
  std::string submitted_prompt;
  std::optional<std::string> provider_request_id;
  std::vector<ImageGenerationCandidate> candidates;
};

absl::Status ValidateImageGenerationSpec(const ImageGenerationSpec& spec,
                                         const ImageGenerationCapabilities& capabilities);
absl::Status ValidateImageGenerationResult(const ImageGenerationResult& result);

// Provider implementations own their asynchronous state here. Poll must not
// block, and Cancel must return promptly even when the remote service does not.
class ImageGenerationOperation {
 public:
  virtual ~ImageGenerationOperation() = default;

  virtual absl::StatusOr<std::optional<ImageGenerationResult>> Poll() = 0;
  virtual void Cancel() noexcept = 0;
};

// RAII owner for one request. An unfinished request is cancelled on destruction
// and never joined on the caller's thread.
class ImageGenerationRequest {
 public:
  static absl::StatusOr<ImageGenerationRequest> Create(
      std::unique_ptr<ImageGenerationOperation> operation);

  ~ImageGenerationRequest();
  ImageGenerationRequest(ImageGenerationRequest&& other) noexcept;
  ImageGenerationRequest& operator=(ImageGenerationRequest&& other) noexcept;

  ImageGenerationRequest(const ImageGenerationRequest&) = delete;
  ImageGenerationRequest& operator=(const ImageGenerationRequest&) = delete;

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll();
  void Cancel() noexcept;
  bool active() const { return operation_ != nullptr; }

 private:
  friend class ImageGenerationClient;

  explicit ImageGenerationRequest(std::unique_ptr<ImageGenerationOperation> operation)
      : operation_(std::move(operation)) {}

  std::unique_ptr<ImageGenerationOperation> operation_;
  size_t maximum_candidates_ = std::numeric_limits<size_t>::max();
};

class ImageGenerationClient {
 public:
  virtual ~ImageGenerationClient() = default;

  virtual ImageGenerationCapabilities Capabilities() const = 0;
  absl::StatusOr<ImageGenerationRequest> Start(ImageGenerationSpec spec);

 protected:
  virtual absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) = 0;
};

}  // namespace zebes
