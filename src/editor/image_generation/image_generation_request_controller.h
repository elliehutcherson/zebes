#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_engine.h"

namespace zebes {

// Mutable provider availability shared by every editor surface. Engines and
// this registry are owned by the composition root and must outlive controllers.
struct ImageGenerationProvider {
  std::string name;
  ImageGenerationEngine* engine = nullptr;
  std::string unavailable_reason;
  bool disable_after_failure = false;

  bool available() const { return engine != nullptr && unavailable_reason.empty(); }
};

struct ImageGenerationProviderRegistry {
  std::vector<ImageGenerationProvider> providers;
};

struct ImageGenerationReview {
  std::string provider;
  std::string model;
  std::string submitted_prompt;
  std::optional<std::string> provider_request_id;
  std::string generated_at_utc;
  std::vector<ImageGenerationCandidate> candidates;
  size_t selected = 0;
};

// Owns one editor surface's remote request and candidate-review lifecycle.
// Prompt defaults, processing, and retained-source writes remain with the
// domain-specific editor.
class ImageGenerationRequestController {
 public:
  static absl::StatusOr<std::unique_ptr<ImageGenerationRequestController>> Create(
      ImageGenerationProviderRegistry* registry);
  ~ImageGenerationRequestController();

  ImageGenerationRequestController(const ImageGenerationRequestController&) = delete;
  ImageGenerationRequestController& operator=(const ImageGenerationRequestController&) = delete;

  const std::vector<ImageGenerationProvider>& providers() const { return registry_->providers; }
  size_t selected_provider() const { return selected_provider_; }
  ImageGenerationCapabilities capabilities() const;
  bool in_flight() const { return pending_.has_value(); }
  const std::optional<ImageGenerationReview>& review() const { return review_; }
  const ImageGenerationCandidate* SelectedCandidate() const;

  absl::Status SelectProvider(size_t index);
  absl::Status Submit(ImageGenerationSpec spec);
  absl::Status Cancel();
  void SelectCandidate(size_t index);
  void DiscardCandidates() { review_.reset(); }
  absl::Status AcceptCandidate(
      const std::function<absl::Status(const ImageGenerationReview&,
                                       const ImageGenerationCandidate&)>& accept);

  // Returns false while the request is still running and true after storing a
  // completed review. A provider failure is returned directly. Only the
  // matching id is collected, so another surface cannot lose its event.
  absl::StatusOr<bool> Poll();

 private:
  struct PendingRequest {
    size_t provider = 0;
    uint64_t id = 0;
    bool cancel_requested = false;
  };

  explicit ImageGenerationRequestController(ImageGenerationProviderRegistry* registry)
      : registry_(registry) {}

  static bool DisablesProvider(const absl::Status& failure);

  ImageGenerationProviderRegistry* registry_;
  size_t selected_provider_ = 0;
  std::optional<PendingRequest> pending_;
  std::optional<ImageGenerationReview> review_;
};

}  // namespace zebes
