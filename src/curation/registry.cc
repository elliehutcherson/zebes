#include "curation/registry.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {

absl::StatusOr<size_t> CurationReviewer::PublishReview(Api& api,
                                                       const CurationReviewRequest& request,
                                                       const std::string& output_path) const {
  absl::StatusOr<CurationReview> review = Review(api, request);
  if (!review.ok()) return review.status();
  const size_t artifact_count = review->artifacts.size();
  const absl::Status publish_status = PublishCurationReview(*review, output_path);
  if (!publish_status.ok()) return publish_status;
  return artifact_count;
}

absl::StatusOr<CurationReview> CurationReviewer::ReviewCandidate(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate) const {
  (void)api;
  (void)request;
  (void)candidate;
  return absl::UnimplementedError(
      absl::StrCat("curation candidates are not supported for kind '", kind(), "'"));
}

absl::StatusOr<size_t> CurationReviewer::PublishCandidateReview(
    Api& api, const CurationReviewRequest& request, const nlohmann::json& candidate,
    const std::string& output_path) const {
  absl::StatusOr<CurationReview> review = ReviewCandidate(api, request, candidate);
  if (!review.ok()) return review.status();
  const size_t artifact_count = review->artifacts.size();
  const absl::Status publish_status = PublishCurationReview(*review, output_path);
  if (!publish_status.ok()) return publish_status;
  return artifact_count;
}

absl::Status CurationReviewer::CommitCandidate(Api& api, const CurationReviewRequest& request,
                                               const nlohmann::json& candidate) const {
  (void)api;
  (void)request;
  (void)candidate;
  return absl::UnimplementedError(
      absl::StrCat("curation candidates cannot be committed for kind '", kind(), "'"));
}

namespace {

absl::Status ValidateRequest(const CurationReviewRequest& request, std::string_view kind) {
  if (request.asset_id.empty()) {
    return absl::InvalidArgumentError("curation review asset ID is empty");
  }
  if (request.focus_entity_id.has_value() && kind != "level") {
    return absl::InvalidArgumentError("focused entity review is supported only for level assets");
  }
  if (request.focus_entity_id.has_value() && *request.focus_entity_id == 0) {
    return absl::InvalidArgumentError("focused entity review ID is invalid");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status CurationRegistry::Add(std::unique_ptr<CurationReviewer> reviewer) {
  if (reviewer == nullptr || reviewer->kind().empty()) {
    return absl::InvalidArgumentError("curation reviewer and kind must be present");
  }
  const std::string kind(reviewer->kind());
  if (!reviewers_.emplace(kind, std::move(reviewer)).second) {
    return absl::AlreadyExistsError(absl::StrCat("curation reviewer already registered: ", kind));
  }
  return absl::OkStatus();
}

absl::StatusOr<CurationReview> CurationRegistry::Review(
    Api& api, std::string_view kind, const CurationReviewRequest& request) const {
  const absl::Status request_status = ValidateRequest(request, kind);
  if (!request_status.ok()) return request_status;
  const auto reviewer = reviewers_.find(kind);
  if (reviewer == reviewers_.end()) {
    return absl::NotFoundError(
        absl::StrCat("no curation reviewer registered for kind '", kind, "'"));
  }
  return reviewer->second->Review(api, request);
}

absl::StatusOr<size_t> CurationRegistry::PublishReview(Api& api, std::string_view kind,
                                                       const CurationReviewRequest& request,
                                                       const std::string& output_path) const {
  const absl::Status request_status = ValidateRequest(request, kind);
  if (!request_status.ok()) return request_status;
  const auto reviewer = reviewers_.find(kind);
  if (reviewer == reviewers_.end()) {
    return absl::NotFoundError(
        absl::StrCat("no curation reviewer registered for kind '", kind, "'"));
  }
  return reviewer->second->PublishReview(api, request, output_path);
}

absl::StatusOr<CurationReview> CurationRegistry::ReviewCandidate(
    Api& api, std::string_view kind, const CurationReviewRequest& request,
    const nlohmann::json& candidate) const {
  const absl::Status request_status = ValidateRequest(request, kind);
  if (!request_status.ok()) return request_status;
  const auto reviewer = reviewers_.find(kind);
  if (reviewer == reviewers_.end()) {
    return absl::NotFoundError(
        absl::StrCat("no curation reviewer registered for kind '", kind, "'"));
  }
  return reviewer->second->ReviewCandidate(api, request, candidate);
}

absl::StatusOr<size_t> CurationRegistry::PublishCandidateReview(
    Api& api, std::string_view kind, const CurationReviewRequest& request,
    const nlohmann::json& candidate, const std::string& output_path) const {
  const absl::Status request_status = ValidateRequest(request, kind);
  if (!request_status.ok()) return request_status;
  const auto reviewer = reviewers_.find(kind);
  if (reviewer == reviewers_.end()) {
    return absl::NotFoundError(
        absl::StrCat("no curation reviewer registered for kind '", kind, "'"));
  }
  return reviewer->second->PublishCandidateReview(api, request, candidate, output_path);
}

absl::Status CurationRegistry::CommitCandidate(Api& api, std::string_view kind,
                                               const CurationReviewRequest& request,
                                               const nlohmann::json& candidate) const {
  const absl::Status request_status = ValidateRequest(request, kind);
  if (!request_status.ok()) return request_status;
  const auto reviewer = reviewers_.find(kind);
  if (reviewer == reviewers_.end()) {
    return absl::NotFoundError(
        absl::StrCat("no curation reviewer registered for kind '", kind, "'"));
  }
  return reviewer->second->CommitCandidate(api, request, candidate);
}

std::vector<std::string> CurationRegistry::Kinds() const {
  std::vector<std::string> kinds;
  kinds.reserve(reviewers_.size());
  for (const auto& [kind, reviewer] : reviewers_) kinds.push_back(kind);
  return kinds;
}

}  // namespace zebes
