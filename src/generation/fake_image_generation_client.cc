#include "generation/fake_image_generation_client.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "common/image_io.h"
#include "generation/image_generation.h"

namespace zebes {
namespace {

RgbaImage FakePixels(const ImageGenerationSpec& spec) {
  if (spec.reference_image.has_value()) {
    RgbaImage image = *spec.reference_image;
    image.pixels.front() ^= 1;
    return image;
  }
  const int greatest = std::max(spec.target_aspect.width, spec.target_aspect.height);
  const int width = std::max(32, 128 * spec.target_aspect.width / greatest);
  const int height = std::max(32, 128 * spec.target_aspect.height / greatest);
  RgbaImage image{
      .width = width,
      .height = height,
      .pixels = std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0),
  };
  const bool transparent = spec.transparency == ImageTransparencyPreference::kPreferTransparent;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const bool subject = !transparent || (x >= width / 5 && x < width * 4 / 5 &&
                                            y >= height / 5 && y < height * 4 / 5);
      if (!subject) continue;
      const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
      image.pixels[offset + 0] = static_cast<uint8_t>(35 + 120 * x / width);
      image.pixels[offset + 1] = static_cast<uint8_t>(45 + 90 * y / height);
      image.pixels[offset + 2] = static_cast<uint8_t>(80 + ((x / 8 + y / 8) % 2) * 50);
      image.pixels[offset + 3] = 255;
    }
  }
  return image;
}

class FakeOperation final : public ImageGenerationOperation {
 public:
  explicit FakeOperation(ImageGenerationSpec spec) : spec_(std::move(spec)) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    if (finished_) return std::nullopt;
    finished_ = true;
    return std::optional<ImageGenerationResult>(ImageGenerationResult{
        .provider = "fake",
        .model = "zebes-fake-v1",
        .submitted_prompt = spec_.prompt,
        .provider_request_id = "fake-request",
        .candidates = {{.image = FakePixels(spec_), .revised_prompt = std::nullopt}},
    });
  }

  void Cancel() noexcept override { finished_ = true; }

 private:
  ImageGenerationSpec spec_;
  bool finished_ = false;
};

class FakeClient final : public ImageGenerationClient {
 public:
  ImageGenerationCapabilities Capabilities() const override {
    return {
        .maximum_candidates = 1,
        .supports_transparency = true,
        .supports_reference_image = true,
    };
  }

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override {
    return ImageGenerationRequest::Create(std::make_unique<FakeOperation>(std::move(spec)));
  }
};

}  // namespace

std::unique_ptr<ImageGenerationClient> CreateFakeImageGenerationClient() {
  return std::make_unique<FakeClient>();
}

}  // namespace zebes
