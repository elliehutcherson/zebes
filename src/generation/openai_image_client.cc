#include "generation/openai_image_client.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

// gpt-image-2 accepts 1-10 images per request.
constexpr int kMaximumCandidates = 10;

void Append(std::vector<uint8_t>& output, std::string_view text) {
  output.insert(output.end(), text.begin(), text.end());
}

void AppendField(std::vector<uint8_t>& output, std::string_view boundary, std::string_view name,
                 std::string_view value) {
  Append(output, absl::StrCat("--", boundary, "\r\nContent-Disposition: form-data; name=\"", name,
                              "\"\r\n\r\n", value, "\r\n"));
}

std::string MultipartBoundary() {
  static std::atomic<uint64_t> sequence = 0;
  return absl::StrCat("zebes-image-edit-", sequence.fetch_add(1, std::memory_order_relaxed));
}

absl::StatusOr<HttpRequest> BuildEditRequest(const OpenAiImageConfig& config,
                                             const ImageGenerationSpec& spec) {
  if (!spec.reference_image.has_value()) {
    return absl::InvalidArgumentError("image edit request needs a reference image");
  }
  ASSIGN_OR_RETURN(const std::vector<uint8_t> image, EncodePng(*spec.reference_image));
  const std::string boundary = MultipartBoundary();
  std::vector<uint8_t> body;
  AppendField(body, boundary, "model", config.model);
  AppendField(body, boundary, "prompt", ComposeImageGenerationPrompt(spec));
  AppendField(body, boundary, "n", absl::StrCat(spec.requested_candidates));
  AppendField(body, boundary, "size", config.size);
  AppendField(body, boundary, "quality", config.quality);
  AppendField(body, boundary, "output_format", "png");
  Append(body, absl::StrCat("--", boundary,
                            "\r\nContent-Disposition: form-data; name=\"image\"; "
                            "filename=\"reference.png\"\r\nContent-Type: image/png\r\n\r\n"));
  body.insert(body.end(), image.begin(), image.end());
  Append(body, absl::StrCat("\r\n--", boundary, "--\r\n"));
  return HttpRequest{
      .method = HttpMethod::kPost,
      .url = config.edit_endpoint,
      .headers = {{.name = "Content-Type",
                   .value = absl::StrCat("multipart/form-data; boundary=", boundary)}},
      .body = std::move(body),
  };
}

absl::Status StatusForHttpCode(int status_code, std::string_view detail) {
  const std::string message =
      detail.empty() ? absl::StrCat("HTTP ", status_code) : std::string(detail);
  switch (status_code) {
    case 400:
      return absl::InvalidArgumentError(
          absl::StrCat("image provider rejected the request: ", message));
    case 401:
      return absl::UnauthenticatedError(
          absl::StrCat("image provider rejected the credential: ", message));
    case 403:
      return absl::PermissionDeniedError(absl::StrCat("image provider denied access: ", message));
    case 404:
      return absl::NotFoundError(absl::StrCat("image provider endpoint not found: ", message));
    case 429:
      return absl::ResourceExhaustedError(
          absl::StrCat("image provider rate limited the request: ", message));
    default:
      break;
  }
  if (status_code >= 500) {
    return absl::UnavailableError(absl::StrCat("image provider is unavailable: ", message));
  }
  if (status_code >= 400) {
    return absl::InvalidArgumentError(absl::StrCat("image provider refused: ", message));
  }
  return absl::DataLossError(
      absl::StrCat("image provider returned an unexpected status: ", message));
}

// nlohmann throws; this adapter owns that boundary so callers stay in Status.
absl::StatusOr<nlohmann::json> ParseJson(const std::vector<uint8_t>& body) {
  nlohmann::json parsed = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
  if (parsed.is_discarded()) {
    return absl::DataLossError("image provider returned a malformed JSON body");
  }
  return parsed;
}

// Error bodies are provider-shaped and may be absent; a missing message is not
// itself a failure, because the status code already carries the outcome.
std::string ErrorDetail(const std::vector<uint8_t>& body) {
  const absl::StatusOr<nlohmann::json> parsed = ParseJson(body);
  if (!parsed.ok()) return "";
  try {
    if (!parsed->contains("error")) return "";
    const nlohmann::json& error = parsed->at("error");
    if (!error.contains("message")) return "";
    return error.at("message").get<std::string>();
  } catch (const nlohmann::json::exception&) {
    return "";
  }
}

class OpenAiImageOperation final : public ImageGenerationOperation {
 public:
  OpenAiImageOperation(HttpRequestHandle request, std::string model, std::string prompt,
                       int64_t maximum_candidate_pixels)
      : request_(std::move(request)),
        model_(std::move(model)),
        prompt_(std::move(prompt)),
        maximum_candidate_pixels_(maximum_candidate_pixels) {}

  absl::StatusOr<std::optional<ImageGenerationResult>> Poll() override {
    ASSIGN_OR_RETURN(std::optional<HttpResponse> response, request_.Poll());
    if (!response.has_value()) return std::nullopt;
    if (response->status_code < 200 || response->status_code >= 300) {
      return StatusForHttpCode(response->status_code, ErrorDetail(response->body));
    }
    ASSIGN_OR_RETURN(ImageGenerationResult result, Decode(*response));
    return std::optional<ImageGenerationResult>(std::move(result));
  }

  void Cancel() noexcept override { request_.Cancel(); }

  absl::Duration SuggestedPollDelay() const override { return request_.SuggestedPollDelay(); }

 private:
  absl::StatusOr<ImageGenerationResult> Decode(const HttpResponse& response) {
    ASSIGN_OR_RETURN(const nlohmann::json body, ParseJson(response.body));

    ImageGenerationResult result{
        .provider = "openai",
        .model = model_,
        .submitted_prompt = prompt_,
    };
    try {
      if (!body.contains("data") || !body.at("data").is_array()) {
        return absl::DataLossError("image provider response has no candidate array");
      }
      for (const nlohmann::json& candidate : body.at("data")) {
        if (!candidate.contains("b64_json")) {
          return absl::DataLossError("image provider candidate has no image data");
        }
        const std::string encoded = candidate.at("b64_json").get<std::string>();
        std::string decoded;
        if (!absl::Base64Unescape(encoded, &decoded)) {
          return absl::DataLossError("image provider candidate is not valid base64");
        }
        const absl::Span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(decoded.data()),
                                              decoded.size());
        ASSIGN_OR_RETURN(RgbaImage image, DecodeImage(bytes, maximum_candidate_pixels_));

        std::optional<std::string> revised;
        if (candidate.contains("revised_prompt") && candidate.at("revised_prompt").is_string()) {
          revised = candidate.at("revised_prompt").get<std::string>();
        }
        result.candidates.push_back(ImageGenerationCandidate{
            .image = std::move(image),
            .revised_prompt = std::move(revised),
        });
      }
    } catch (const nlohmann::json::exception& error) {
      return absl::DataLossError(
          absl::StrCat("image provider response could not be read: ", error.what()));
    }

    if (result.candidates.empty()) {
      return absl::NotFoundError("image provider returned no candidates");
    }
    return result;
  }

  HttpRequestHandle request_;
  std::string model_;
  std::string prompt_;
  int64_t maximum_candidate_pixels_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<OpenAiImageClient>> OpenAiImageClient::Create(
    HttpTransport& transport, const CredentialSource& credentials, OpenAiImageConfig config) {
  if (config.endpoint.empty() || config.edit_endpoint.empty() || config.model.empty() ||
      config.credential_reference.empty()) {
    return absl::InvalidArgumentError(
        "OpenAI image client needs generation/edit endpoints, a model, and credential reference");
  }
  if (config.maximum_candidate_pixels <= 0) {
    return absl::InvalidArgumentError("OpenAI image client needs a positive pixel limit");
  }
  return std::unique_ptr<OpenAiImageClient>(
      new OpenAiImageClient(transport, credentials, std::move(config)));
}

OpenAiImageClient::OpenAiImageClient(HttpTransport& transport, const CredentialSource& credentials,
                                     OpenAiImageConfig config)
    : transport_(transport), credentials_(credentials), config_(std::move(config)) {}

ImageGenerationCapabilities OpenAiImageClient::Capabilities() const {
  return ImageGenerationCapabilities{
      .maximum_candidates = kMaximumCandidates,
      .supports_negative_prompt = false,
      // gpt-image-2 rejects background=transparent; isolation removes the
      // background downstream, as it already does for imported sources.
      .supports_transparency = false,
      .supports_reference_image = true,
  };
}

absl::StatusOr<ImageGenerationRequest> OpenAiImageClient::StartValidated(ImageGenerationSpec spec) {
  // Loaded per request so no secret outlives the request that used it, and a
  // missing key is one request's failure rather than a startup failure.
  ASSIGN_OR_RETURN(SecretString secret, credentials_.Load(config_.credential_reference));
  ASSIGN_OR_RETURN(SecretString authorization,
                   SecretString::Create(absl::StrCat("Bearer ", secret.value())));

  HttpRequest request;
  if (spec.reference_image.has_value()) {
    ASSIGN_OR_RETURN(request, BuildEditRequest(config_, spec));
  } else {
    const nlohmann::json payload{
        {"model", config_.model},         {"prompt", ComposeImageGenerationPrompt(spec)},
        {"n", spec.requested_candidates}, {"size", config_.size},
        {"quality", config_.quality},     {"output_format", "png"},
    };
    const std::string body = payload.dump();
    request = HttpRequest{
        .method = HttpMethod::kPost,
        .url = config_.endpoint,
        .headers = {{.name = "Content-Type", .value = "application/json"}},
        .body = std::vector<uint8_t>(body.begin(), body.end()),
    };
  }
  request.sensitive_headers.push_back(HttpSensitiveHeader{
      .name = "Authorization",
      .value = std::move(authorization),
  });

  ASSIGN_OR_RETURN(HttpRequestHandle handle, transport_.Start(std::move(request)));
  return ImageGenerationRequest::Create(std::make_unique<OpenAiImageOperation>(
      std::move(handle), config_.model, std::move(spec.prompt), config_.maximum_candidate_pixels));
}

}  // namespace zebes
