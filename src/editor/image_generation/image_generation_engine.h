#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/engine.h"
#include "common/mpsc_queue.h"
#include "common/notification.h"
#include "common/notification_set.h"
#include "editor/image_generation/image_generation.h"

namespace zebes {

// One finished generation, successful or not. Every accepted Submit produces
// exactly one of these, including a cancelled or rejected one, which is what
// lets the engine bound how much work it will accept.
struct GenerationEvent {
  uint64_t id = 0;
  absl::StatusOr<ImageGenerationResult> result;
};

// A long-lived owner for every in-flight image-generation request.
//
// One of these lives for the editor session rather than for a request. It costs
// nothing while no request is in flight: Run reports idle with no deadline and
// its runner sleeps on the command notification until Submit wakes it.
//
// Threading: Submit and Cancel are safe from any thread. NextEvent belongs to
// the thread that owns the results, because accepting a candidate commits
// resource-manager and GPU state. Run belongs to the EngineRunner's thread and
// is called only by it.
//
// The engine holds the client, and so the credential reference, for its whole
// life. Secrets themselves do not: a client loads one per request from its
// CredentialSource, so no SecretString outlives the request that used it.
class ImageGenerationEngine final : public Engine {
 public:
  // The most requests that may be submitted and not yet collected. Each one
  // owns a slot in the event queue for its whole life, which is what makes
  // delivering its event infallible. Requests are slow and remote; a deep
  // queue would only hide a runaway caller.
  static constexpr size_t kMaxOutstandingRequests = 8;

  static absl::StatusOr<std::unique_ptr<ImageGenerationEngine>> Create(
      std::unique_ptr<ImageGenerationClient> client);

  NotificationSet& notification_set() override { return *notification_set_; }

  // Constant for the client's life, so any thread may ask. Callers bound their
  // own controls with it rather than discovering a refusal at Submit.
  ImageGenerationCapabilities Capabilities() const { return client_->Capabilities(); }

  absl::StatusOr<RunResult> Run() override;

  // Queues a request and returns the id its event will carry. Rejects with
  // ResourceExhausted once kMaxOutstandingRequests are outstanding; the caller
  // reports that rather than queueing behind it. The spec is validated on the
  // engine thread, so an invalid one is reported as this id's event.
  absl::StatusOr<uint64_t> Submit(ImageGenerationSpec spec);

  // Asks the engine to abandon a request. The request reports Cancelled like
  // any other outcome. An id that already finished is ignored: its event is
  // already on its way, and a second one would be a result the caller never
  // asked for.
  absl::Status Cancel(uint64_t id);

  // Collects one finished request, or nothing. Non-blocking, so a frame loop
  // can drain it without ever waiting on the engine.
  std::optional<GenerationEvent> NextEvent();

 private:
  struct StartGeneration {
    uint64_t id = 0;
    ImageGenerationSpec spec;
  };

  struct CancelGeneration {
    uint64_t id = 0;
  };

  using GenerationCommand = std::variant<StartGeneration, CancelGeneration>;

  // Bounded by outstanding requests, plus room for a cancel for each of them.
  static constexpr size_t kCommandCapacity = 2 * kMaxOutstandingRequests;

  ImageGenerationEngine(std::unique_ptr<ImageGenerationClient> client,
                        std::unique_ptr<NotificationSet> notification_set,
                        Notification& command_notification);

  // True when anything moved, which is what tells the runner to skip sleeping.
  bool ApplyCommands();
  bool PollRequests();
  void Start(StartGeneration command);
  void Finish(uint64_t id, absl::StatusOr<ImageGenerationResult> result);

  std::unique_ptr<ImageGenerationClient> client_;

  // Destroyed after the queues that hold its notifications.
  std::unique_ptr<NotificationSet> notification_set_;
  MpscNotifyQueue<GenerationCommand, kCommandCapacity> commands_;

  // No notification: results are collected by a thread that polls on its own
  // schedule and never sleeps on this engine.
  MpscQueue<GenerationEvent, kMaxOutstandingRequests> events_;

  // Submitted and not yet collected. Reserving a slot before the command is
  // queued is what keeps every later event push infallible.
  std::atomic<size_t> outstanding_ = 0;
  std::atomic<uint64_t> next_id_ = 1;

  // Engine-thread only.
  absl::flat_hash_map<uint64_t, ImageGenerationRequest> in_flight_;
};

}  // namespace zebes
