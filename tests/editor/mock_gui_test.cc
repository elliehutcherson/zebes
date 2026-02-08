#include "mock_gui.h"

#include "gtest/gtest.h"

namespace zebes {
namespace {

TEST(MockGuiTest, Instantiate) {
  MockGui mock_gui;
  // Just verify we can instantiate it and it links correctly.
  EXPECT_TRUE(true);
}

}  // namespace
}  // namespace zebes
