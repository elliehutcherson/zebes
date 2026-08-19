#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "common/notification.h"

namespace zebes {

// Owns one native blocking facility and every Notification that feeds it.
//
// An engine creates a set while it constructs its queues and wait handles, adds
// one source per wake reason, and exposes the set to its EngineRunner. Sealing
// fixes the source list before a worker thread can start, so the list is never
// mutated while a thread is armed or waiting.
//
// Arm, Disarm, and Wait belong to the runner that owns the engine's thread. An
// engine must not call them: Engine::Run may not block.
class NotificationSet {
 public:
  ~NotificationSet();

  NotificationSet(const NotificationSet&) = delete;
  NotificationSet& operator=(const NotificationSet&) = delete;

  static absl::StatusOr<std::unique_ptr<NotificationSet>> Create();

  // Adds a source signaled by Notification::Notify. The returned pointer is
  // never null and stays valid for the set's lifetime.
  absl::StatusOr<Notification*> AddSoftware();

  // A software source whose callbacks bracket every blocking wait, for a
  // producer that must be told when the worker is about to sleep.
  absl::StatusOr<Notification*> AddSoftware(NotificationCallbacks callbacks);

  // Adds a source signaled by a native wait handle the caller owns and keeps
  // open for the set's lifetime. The callbacks bracket every blocking wait, so
  // an interrupt-driven source arms only while the worker is about to sleep.
  absl::StatusOr<Notification*> AddExternal(NativeWaitHandle native_wait_handle,
                                            NotificationCallbacks callbacks);

  // Fixes the source list. EngineRunner::Create seals the engine's set, so a
  // second runner for the same engine fails instead of racing the first.
  absl::Status Seal();

  // Arms every source and publishes the arm flag producers read. The caller
  // must poll for work once more after this returns and before it calls Wait:
  // a producer that observed the set unarmed skipped its wake, and that recheck
  // is what delivers the work instead.
  absl::Status Arm();
  void Disarm() noexcept;

  // Blocks until a source fires.
  absl::Status Wait();

  // Blocks until a source fires or `deadline` passes, whichever comes first. A
  // deadline already in the past checks the sources once and returns.
  //
  // Both outcomes return OK, because the caller cannot act on the difference:
  // a wake still has to poll its sources to find out which one fired, and that
  // same poll is what a timeout needs. An interrupted wait resumes against the
  // original deadline rather than restarting it.
  absl::Status WaitUntil(absl::Time deadline);

  size_t size() const { return notifications_.size(); }
  bool sealed() const { return sealed_; }

 private:
  class Impl;

  explicit NotificationSet(std::unique_ptr<Impl> impl);

  absl::Status CheckMutable() const;
  absl::Status WaitInternal(std::optional<absl::Time> deadline);

  // Declaration order is load-bearing: every Notification holds a reference to
  // *impl_, so the notifications must be destroyed first.
  std::unique_ptr<Impl> impl_;
  std::vector<std::unique_ptr<Notification>> notifications_;
  absl::flat_hash_set<intptr_t> registered_wait_handles_;
  size_t armed_count_ = 0;
  bool sealed_ = false;
};

}  // namespace zebes
