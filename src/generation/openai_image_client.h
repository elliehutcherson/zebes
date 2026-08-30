#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "generation/credential_source.h"
#include "generation/http_transport.h"
#include "generation/image_generation.h"

namespace zebes {

// Versioned adapter settings. A typed struct rather than a JSON bag, so a
// provider change is a compile error instead of a silently ignored key.
struct OpenAiImageConfig {
  std::string endpoint = "https://api.openai.com/v1/images/generations";
  std::string edit_endpoint = "https://api.openai.com/v1/images/edits";
  std::string model = "gpt-image-2";

  // The environment variable holding the key, never the key itself.
  std::string credential_reference = "OPENAI_API_KEY";

  std::string size = "1024x1024";
  std::string quality = "high";

  int64_t maximum_candidate_pixels = 4096 * 4096;
  int maximum_reference_images = 16;
  int64_t maximum_reference_pixels = 16 * 1024 * 1024;
};

// Adapter for OpenAI's image generations endpoint.
//
// The request is one POST that returns the finished images, so an operation is
// a single HTTP request and polling forwards to the transport unchanged.
class OpenAiImageClient final : public ImageGenerationClient {
 public:
  // `transport` and `credentials` must outlive the client.
  static absl::StatusOr<std::unique_ptr<OpenAiImageClient>> Create(
      HttpTransport& transport, const CredentialSource& credentials, OpenAiImageConfig config);

  ImageGenerationCapabilities Capabilities() const override;

 protected:
  absl::StatusOr<ImageGenerationRequest> StartValidated(ImageGenerationSpec spec) override;

 private:
  OpenAiImageClient(HttpTransport& transport, const CredentialSource& credentials,
                    OpenAiImageConfig config);

  HttpTransport& transport_;
  const CredentialSource& credentials_;
  OpenAiImageConfig config_;
};

}  // namespace zebes
