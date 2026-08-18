#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "common/notification.h"

namespace zebes {

// A fixed-capacity, lock-free multi-producer/single-consumer queue.
//
// Producers claim preallocated slots and publish them to an atomic stack. The
// sole consumer detaches and reverses each batch to preserve FIFO publication
// order. TryPush returns false under backpressure without moving from `value`.
// Destruction requires all producers and the consumer to have stopped.
template <typename T, size_t Capacity>
class MpscQueue {
 public:
  static_assert(Capacity > 0, "An MPSC queue must have positive capacity");
  static_assert(Capacity < std::numeric_limits<size_t>::max(),
                "MPSC queue capacity must leave room for its empty sentinel");
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "MPSC queue values must be nothrow move constructible");
  static_assert(std::is_nothrow_destructible_v<T>,
                "MPSC queue values must be nothrow destructible");
  static_assert(std::atomic<size_t>::is_always_lock_free,
                "MPSC queue indices require an always-lock-free atomic<size_t>");
  static_assert(std::atomic<uint8_t>::is_always_lock_free,
                "MPSC queue slot state requires an always-lock-free atomic<uint8_t>");

  MpscQueue() = default;

  MpscQueue(const MpscQueue&) = delete;
  MpscQueue& operator=(const MpscQueue&) = delete;

  // Publishes value if a preallocated slot is available. On failure, value is
  // unchanged so the producer can retry or apply its own backpressure policy.
  bool TryPush(T&& value) noexcept {
    size_t index = next_slot_.fetch_add(1, std::memory_order_relaxed) % Capacity;
    for (size_t visited = 0; visited < Capacity; ++visited) {
      uint8_t expected = kFree;
      if (slots_[index].state.compare_exchange_strong(
              expected, kReserved, std::memory_order_acquire, std::memory_order_relaxed)) {
        Publish(index, std::move(value));
        return true;
      }
      ++index;
      if (index == Capacity) {
        index = 0;
      }
    }
    return false;
  }

  // Returns the oldest published value. Only the consumer thread may call
  // TryPop, though producers may call TryPush concurrently.
  std::optional<T> TryPop() noexcept {
    if (consumer_head_ == kEmpty) {
      AcquireBatch();
    }
    if (consumer_head_ == kEmpty) {
      return std::nullopt;
    }

    const size_t index = consumer_head_;
    Slot& slot = slots_[index];
    consumer_head_ = slot.next;

    std::optional<T> result(std::in_place, std::move(*slot.value));
    slot.value.reset();
    slot.state.store(kFree, std::memory_order_release);
    return result;
  }

 private:
  static constexpr uint8_t kFree = 0;
  static constexpr uint8_t kReserved = 1;
  static constexpr size_t kEmpty = Capacity;

  struct Slot {
    std::atomic<uint8_t> state = kFree;
    size_t next = kEmpty;
    std::optional<T> value;
  };

  void Publish(size_t index, T&& value) noexcept {
    Slot& slot = slots_[index];
    slot.value.emplace(std::move(value));

    size_t head = producer_head_.load(std::memory_order_relaxed);
    do {
      slot.next = head;
    } while (!producer_head_.compare_exchange_weak(head, index, std::memory_order_acq_rel,
                                                   std::memory_order_relaxed));
  }

  void AcquireBatch() noexcept {
    size_t head = producer_head_.exchange(kEmpty, std::memory_order_acquire);
    size_t reversed = kEmpty;
    while (head != kEmpty) {
      const size_t next = slots_[head].next;
      slots_[head].next = reversed;
      reversed = head;
      head = next;
    }
    consumer_head_ = reversed;
  }

  std::array<Slot, Capacity> slots_;
  std::atomic<size_t> next_slot_ = 0;
  std::atomic<size_t> producer_head_ = kEmpty;
  size_t consumer_head_ = kEmpty;
};

// An MPSC queue that notifies a waiter after each successful publication.
// Multiple queues can share one Notification to wake the same consumer.
template <typename T, size_t Capacity>
class MpscNotifyQueue {
 public:
  explicit MpscNotifyQueue(Notification& notification) : notification_(notification) {}

  MpscNotifyQueue(const MpscNotifyQueue&) = delete;
  MpscNotifyQueue& operator=(const MpscNotifyQueue&) = delete;

  bool TryPush(T&& value) noexcept {
    if (!queue_.TryPush(std::move(value))) {
      return false;
    }
    notification_.Notify();
    return true;
  }

  std::optional<T> TryPop() noexcept { return queue_.TryPop(); }

 private:
  Notification& notification_;
  MpscQueue<T, Capacity> queue_;
};

}  // namespace zebes
