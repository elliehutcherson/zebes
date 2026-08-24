#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "editor/gui_interface.h"
#include "editor/image_generation/image_generation.h"
#include "editor/image_generation/image_generation_request_controller.h"

namespace zebes {

struct ImageGenerationProviderStatus {
  std::string name;
  bool available = false;
  std::string unavailable_reason;
};

struct ImageGenerationUiState {
  std::vector<ImageGenerationProviderStatus> providers;
  size_t selected_provider = 0;
  ImageGenerationCapabilities capabilities;
  bool in_flight = false;
  const ImageGenerationReview* review = nullptr;
  size_t selected_candidate = 0;
};

enum class ImageGenerationLifecycleAction {
  kNone,
  kSelectProvider,
  kCancel,
  kSelectCandidate,
  kAcceptCandidate,
  kDiscardCandidates,
};

struct ImageGenerationLifecycleResult {
  ImageGenerationLifecycleAction action = ImageGenerationLifecycleAction::kNone;
  bool show_draft = false;
};

struct ImageGenerationLifecyclePanelOptions {
  std::string_view editor_id;
  float provider_width = 180.0f;
  bool can_accept_candidate = true;
  const char* acceptance_blocked_message = nullptr;
};

ImageGenerationUiState BuildImageGenerationUiState(
    const ImageGenerationRequestController& controller);

// Renders the provider/request/review portion shared by generated-artwork
// workflows. Domain editors render their own prompt and processing controls
// only when show_draft is true.
ImageGenerationLifecycleResult RenderImageGenerationLifecycle(
    GuiInterface& gui, const ImageGenerationLifecyclePanelOptions& options,
    ImageGenerationUiState& state);

}  // namespace zebes
