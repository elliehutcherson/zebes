#include "common/mpsc_queue.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(MpscQueueTest, PreservesFifoOrder) {
  MpscQueue<int, 3> queue;
  int first = 1;
  int second = 2;
  int third = 3;

  ASSERT_TRUE(queue.TryPush(std::move(first)));
  ASSERT_TRUE(queue.TryPush(std::move(second)));
  ASSERT_TRUE(queue.TryPush(std::move(third)));

  EXPECT_EQ(queue.TryPop(), 1);
  EXPECT_EQ(queue.TryPop(), 2);
  EXPECT_EQ(queue.TryPop(), 3);
  EXPECT_EQ(queue.TryPop(), std::nullopt);
}

TEST(MpscQueueTest, AppliesBackpressureWithoutConsumingTheRejectedValue) {
  MpscQueue<std::string, 2> queue;
  std::string first = "first";
  std::string second = "second";
  std::string rejected = "retry me";

  ASSERT_TRUE(queue.TryPush(std::move(first)));
  ASSERT_TRUE(queue.TryPush(std::move(second)));
  EXPECT_FALSE(queue.TryPush(std::move(rejected)));
  EXPECT_EQ(rejected, "retry me");

  ASSERT_EQ(queue.TryPop(), "first");
  EXPECT_TRUE(queue.TryPush(std::move(rejected)));
  EXPECT_EQ(queue.TryPop(), "second");
  EXPECT_EQ(queue.TryPop(), "retry me");
}

struct Message {
  size_t producer = 0;
  size_t sequence = 0;

  friend bool operator==(const Message&, const Message&) = default;
};

TEST(MpscQueueTest, DeliversEveryItemFromConcurrentProducersInProducerOrder) {
  constexpr size_t kProducerCount = 4;
  constexpr size_t kItemsPerProducer = 2000;
  MpscQueue<Message, 64> queue;
  std::vector<std::thread> producers;
  producers.reserve(kProducerCount);

  for (size_t producer = 0; producer < kProducerCount; ++producer) {
    producers.emplace_back([producer, &queue] {
      for (size_t sequence = 0; sequence < kItemsPerProducer; ++sequence) {
        Message message{.producer = producer, .sequence = sequence};
        while (!queue.TryPush(std::move(message))) {
          std::this_thread::yield();
        }
      }
    });
  }

  std::array<size_t, kProducerCount> next_sequence{};
  size_t received = 0;
  while (received < kProducerCount * kItemsPerProducer) {
    std::optional<Message> message = queue.TryPop();
    if (!message.has_value()) {
      std::this_thread::yield();
      continue;
    }
    ASSERT_LT(message->producer, kProducerCount);
    EXPECT_EQ(message->sequence, next_sequence[message->producer]);
    ++next_sequence[message->producer];
    ++received;
  }

  for (std::thread& producer : producers) {
    producer.join();
  }
  for (size_t sequence : next_sequence) {
    EXPECT_EQ(sequence, kItemsPerProducer);
  }
}

}  // namespace
}  // namespace zebes
