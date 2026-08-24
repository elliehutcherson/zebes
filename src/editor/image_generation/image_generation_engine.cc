#include "editor/image_generation/image_generation_engine.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<ImageGenerationEngine>> ImageGenerationEngine::Create(
    std::unique_ptr<ImageGenerationClient> client) {
  if (client == nullptr) {
    return absl::InvalidArgumentError("image generation engine needs a client");
  }
  ASSIGN_OR_RETURN(std::unique_ptr<NotificationSet> notification_set, NotificationSet::Create());
  ASSIGN_OR_RETURN(Notification * command_notification, notification_set->AddSoftware());
  return std::unique_ptr<ImageGenerationEngine>(new ImageGenerationEngine(
      std::move(client), std::move(notification_set), *command_notification));
}

ImageGenerationEngine::ImageGenerationEngine(std::unique_ptr<ImageGenerationClient> client,
                                             std::unique_ptr<NotificationSet> notification_set,
                                             Notification& command_notification)
    : client_(std::move(client)),
      notification_set_(std::move(notification_set)),
      commands_(command_notification) {}

absl::StatusOr<uint64_t> ImageGenerationEngine::Submit(ImageGenerationSpec spec) {
  // Claim the slot before queueing. A slot is what guarantees this request's
  // event can be delivered, so it has to be held from here until NextEvent
  // hands the event over.
  size_t outstanding = outstanding_.load(std::memory_order_acquire);
  do {
    if (outstanding >= kMaxOutstandingRequests) {
      return absl::ResourceExhaustedError("too many image generation requests are outstanding");
    }
  } while (!outstanding_.compare_exchange_weak(
      outstanding, outstanding + 1, std::memory_order_acq_rel, std::memory_order_acquire));

  const uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
  GenerationCommand command = StartGeneration{.id = id, .spec = std::move(spec)};
  if (!commands_.TryPush(std::move(command))) {
    outstanding_.fetch_sub(1, std::memory_order_acq_rel);
    return absl::ResourceExhaustedError("the image generation command queue is full");
  }
  return id;
}

absl::Status ImageGenerationEngine::Cancel(uint64_t id) {
  GenerationCommand command = CancelGeneration{.id = id};
  if (!commands_.TryPush(std::move(command))) {
    return absl::ResourceExhaustedError("the image generation command queue is full");
  }
  return absl::OkStatus();
}

std::optional<GenerationEvent> ImageGenerationEngine::NextEvent() {
  if (!ready_events_.empty()) {
    auto found = ready_events_.begin();
    GenerationEvent event = std::move(found->second);
    ready_events_.erase(found);
    outstanding_.fetch_sub(1, std::memory_order_acq_rel);
    return event;
  }
  std::optional<GenerationEvent> event = events_.TryPop();
  if (event.has_value()) {
    outstanding_.fetch_sub(1, std::memory_order_acq_rel);
  }
  return event;
}

std::optional<GenerationEvent> ImageGenerationEngine::NextEvent(uint64_t id) {
  if (auto found = ready_events_.find(id); found != ready_events_.end()) {
    GenerationEvent event = std::move(found->second);
    ready_events_.erase(found);
    outstanding_.fetch_sub(1, std::memory_order_acq_rel);
    return event;
  }

  while (std::optional<GenerationEvent> event = events_.TryPop()) {
    if (event->id == id) {
      outstanding_.fetch_sub(1, std::memory_order_acq_rel);
      return event;
    }
    const uint64_t event_id = event->id;
    ABSL_CHECK(ready_events_.emplace(event_id, std::move(*event)).second)
        << "image generation engine produced duplicate event id " << event_id;
  }
  return std::nullopt;
}

absl::StatusOr<RunResult> ImageGenerationEngine::Run() {
  // Commands first: a request submitted this pass gets started and polled
  // before the pass decides how long the runner may sleep.
  const bool applied = ApplyCommands();
  const bool polled = PollRequests();
  if (applied || polled) {
    return RunResult{.feedback = RunFeedback::kDidWork};
  }
  if (in_flight_.empty()) {
    // Every remaining wake source has a notification, so the runner may sleep
    // until Submit or Cancel signals one. This is where a session spends
    // almost all of its time.
    return RunResult{.feedback = RunFeedback::kIdle};
  }

  // An in-flight request has no descriptor the runner can wait on, so the
  // soonest any of them wants attention is the whole of what bounds this sleep.
  absl::Duration soonest = absl::InfiniteDuration();
  for (const auto& [id, request] : in_flight_) {
    soonest = std::min(soonest, request.SuggestedPollDelay());
  }
  return RunResult{
      .feedback = RunFeedback::kIdle,
      .wake_deadline = absl::Now() + soonest,
  };
}

bool ImageGenerationEngine::ApplyCommands() {
  bool applied = false;
  while (std::optional<GenerationCommand> command = commands_.TryPop()) {
    applied = true;
    if (auto* start = std::get_if<StartGeneration>(&*command)) {
      Start(std::move(*start));
      continue;
    }
    const uint64_t id = std::get<CancelGeneration>(*command).id;
    // Erasing destroys the request, which cancels it without joining. An id
    // that already finished is not here, and its event is already queued.
    if (in_flight_.erase(id) > 0) {
      Finish(id, absl::CancelledError("image generation was cancelled"));
    }
  }
  return applied;
}

void ImageGenerationEngine::Start(StartGeneration command) {
  absl::StatusOr<ImageGenerationRequest> request = client_->Start(std::move(command.spec));
  if (!request.ok()) {
    // A rejected spec is this request's outcome, not the engine's failure. An
    // engine error would end the runner and take every other request with it.
    Finish(command.id, request.status());
    return;
  }
  in_flight_.try_emplace(command.id, *std::move(request));
}

bool ImageGenerationEngine::PollRequests() {
  // Collected rather than delivered in place, because erasing from in_flight_
  // while iterating it would invalidate the iterator this loop is holding.
  std::vector<GenerationEvent> finished;
  for (auto& [id, request] : in_flight_) {
    absl::StatusOr<std::optional<ImageGenerationResult>> polled = request.Poll();
    if (!polled.ok()) {
      finished.push_back(GenerationEvent{.id = id, .result = polled.status()});
      continue;
    }
    if (!polled->has_value()) continue;
    finished.push_back(GenerationEvent{.id = id, .result = *std::move(*polled)});
  }

  for (GenerationEvent& event : finished) {
    in_flight_.erase(event.id);
    Finish(event.id, std::move(event.result));
  }
  return !finished.empty();
}

void ImageGenerationEngine::Finish(uint64_t id, absl::StatusOr<ImageGenerationResult> result) {
  GenerationEvent event{.id = id, .result = std::move(result)};
  // Infallible by construction: Submit reserved a slot for this request and
  // holds it until NextEvent delivers the event, and a request produces
  // exactly one event. A failure here means that accounting is broken.
  ABSL_CHECK(events_.TryPush(std::move(event)))
      << "image generation event queue overflowed its reserved slots";
}

}  // namespace zebes
