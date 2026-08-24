#include "resources/resource_utils.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

using ::testing::HasSubstr;

class ResourceUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
                 ("zebes-resource-utils-" + std::to_string(++sequence));
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  static int sequence;
  std::filesystem::path directory_;
};

int ResourceUtilsTest::sequence = 0;

TEST_F(ResourceUtilsTest, LoadsEveryJsonAndAggregatesFailures) {
  std::ofstream(directory_ / "good.json") << "{}";
  std::ofstream(directory_ / "bad.json") << "{}";
  std::ofstream(directory_ / "ignored.txt") << "{}";

  std::set<std::string> visited;
  const absl::Status status = LoadJsonDefinitions(
      directory_.string(), "test", [&visited](const std::filesystem::path& path) -> absl::Status {
        visited.insert(path.filename().string());
        if (path.stem() == "bad") return absl::InvalidArgumentError("invalid contents");
        return absl::OkStatus();
      });

  EXPECT_EQ(visited, (std::set<std::string>{"bad.json", "good.json"}));
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);
  EXPECT_THAT(std::string(status.message()), HasSubstr("bad.json (invalid contents)"));
}

TEST_F(ResourceUtilsTest, MissingDirectoryIsNotAnEmptyCatalog) {
  std::filesystem::remove_all(directory_);

  const absl::Status status = LoadJsonDefinitions(
      directory_.string(), "test",
      [](const std::filesystem::path&) -> absl::Status { return absl::OkStatus(); });

  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(ResourceUtilsTest, AtomicWriterCreatesParentsAndReplacesCompleteContents) {
  const std::filesystem::path path = directory_ / "nested" / "definition.json";

  EXPECT_TRUE(WriteTextFileAtomically(path.string(), "first").ok());
  EXPECT_TRUE(WriteTextFileAtomically(path.string(), "replacement").ok());

  std::ifstream stream(path);
  std::string contents;
  std::getline(stream, contents, '\0');
  EXPECT_EQ(contents, "replacement");
  EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
}

}  // namespace
}  // namespace zebes
