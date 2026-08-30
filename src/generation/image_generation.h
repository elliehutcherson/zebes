#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
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

enum class ImageGenerationReferenceRole : uint8_t {
  kEditSource = 0,
  kSubjectIdentity = 1,
  kPose = 2,
};

struct ImageGenerationReference {
  ImageGenerationReferenceRole role = ImageGenerationReferenceRole::kEditSource;
  RgbaImage image;
};

struct ImageGenerationSpec {
  std::string prompt;
  // Provider-neutral guidance applied before the subject prompt. Providers
  // that do not expose a distinct system-instruction field compose this into
  // their request text; submitted_prompt remains the user's subject prompt.
  std::optional<std::string> instructions;
  std::optional<std::string> negative_prompt;
  int requested_candidates = 1;
  ImageAspectRatio target_aspect;
  ImageTransparencyPreference transparency = ImageTransparencyPreference::kNoPreference;
  // Order is provider-visible. An optional edit source is unique and must be
  // first; caller-specific workflows may impose stricter role counts.
  std::vector<ImageGenerationReference> references;
};

struct ImageGenerationCapabilities {
  int maximum_candidates = 1;
  bool supports_negative_prompt = false;
  bool supports_transparency = false;
  // Zero for both fields means references are unsupported. The pixel limit is
  // aggregate decoded RGBA pixels across one request; at four bytes per pixel
  // it also bounds the owned reference byte total.
  int maximum_reference_images = 0;
  int64_t maximum_reference_pixels = 0;
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
std::string_view ImageGenerationReferenceRoleName(ImageGenerationReferenceRole role);
absl::StatusOr<ImageGenerationReferenceRole> ParseImageGenerationReferenceRole(
    std::string_view value);
// Composes the provider-neutral user-turn body: indexed reference semantics
// followed by the subject request. Codex keeps developer instructions separate
// and uses this form directly.
std::string ComposeImageGenerationTurnPrompt(const ImageGenerationSpec& spec);
// Flattens optional instructions and the shared turn body for providers that
// expose only one prompt field.
std::string ComposeImageGenerationPrompt(const ImageGenerationSpec& spec);
absl::Status ValidateImageGenerationResult(const ImageGenerationResult& result);

// Provider implementations own their asynchronous state here. Poll must not
// block, and Cancel must return promptly even when the remote service does not.
class ImageGenerationOperation {
 public:
  virtual ~ImageGenerationOperation() = default;

  virtual absl::StatusOr<std::optional<ImageGenerationResult>> Poll() = 0;
  virtual void Cancel() noexcept = 0;

  // The longest a caller may wait before calling Poll again. An adapter built
  // on HttpRequestHandle should forward the handle's answer rather than invent
  // one, so the transport's own timer decides the cadence.
  virtual absl::Duration SuggestedPollDelay() const { return absl::Milliseconds(50); }
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

  // Zero once the request has finished, so a caller sleeping on this value
  // polls immediately and learns the request is no longer active.
  absl::Duration SuggestedPollDelay() const;

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
