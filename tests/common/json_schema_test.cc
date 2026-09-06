#include "common/json_schema.h"

#include <string>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"

namespace zebes::json_schema {
namespace {

using ::testing::HasSubstr;

TEST(RequiredTest, ReadsAPresentField) {
  const nlohmann::json json = {{"name", "Catacombs"}, {"width", 320}};

  ASSERT_OK_AND_ASSIGN(const std::string name, Required<std::string>(json, "name", "level"));
  ASSERT_OK_AND_ASSIGN(const int width, Required<int>(json, "width", "level"));

  EXPECT_EQ(name, "Catacombs");
  EXPECT_EQ(width, 320);
}

TEST(RequiredTest, RefusesAMissingFieldRatherThanReturningADefault) {
  const nlohmann::json json = {{"name", "Catacombs"}};

  const absl::Status status = Required<int>(json, "width", "level").status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("level is missing 'width'"));
}

TEST(RequiredTest, RefusesAFieldOfTheWrongType) {
  const nlohmann::json json = {{"width", "three hundred"}};

  const absl::Status status = Required<int>(json, "width", "level").status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("level field 'width' is invalid"));
}

// A non-object reports the field as missing rather than throwing out of the
// nlohmann accessor, so callers stay entirely in the status error model.
TEST(RequiredTest, RefusesANonObjectWithoutThrowing) {
  const nlohmann::json json = nlohmann::json::array({1, 2});

  const absl::Status status = Required<int>(json, "width", "level").status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("level is missing 'width'"));
}

TEST(RequireExactObjectTest, AcceptsExactlyTheExpectedKeys) {
  const nlohmann::json json = {{"id", "a"}, {"name", "b"}};

  EXPECT_OK(RequireExactObject(json, {"id", "name"}, "level"));
}

TEST(RequireExactObjectTest, RefusesAnUnknownField) {
  const nlohmann::json json = {{"id", "a"}, {"name", "b"}, {"colour", "red"}};

  const absl::Status status = RequireExactObject(json, {"id", "name"}, "level");

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("level contains unknown field 'colour'"));
}

TEST(RequireExactObjectTest, RefusesAMissingField) {
  const nlohmann::json json = {{"id", "a"}};

  const absl::Status status = RequireExactObject(json, {"id", "name"}, "level");

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("level is missing 'name'"));
}

TEST(RequireExactObjectTest, RefusesANonObject) {
  const absl::Status status = RequireExactObject(nlohmann::json::array({1}), {"id"}, "level");

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), HasSubstr("level must be an object"));
}

// A renamed field arrives as one unknown key and one missing key. Reporting the
// unknown one first names the key actually present in the file.
TEST(RequireExactObjectTest, ReportsTheUnknownFieldWhenAFieldWasRenamed) {
  const nlohmann::json json = {{"id", "a"}, {"title", "b"}};

  const absl::Status status = RequireExactObject(json, {"id", "name"}, "level");

  EXPECT_THAT(std::string(status.message()), HasSubstr("unknown field 'title'"));
}

}  // namespace
}  // namespace zebes::json_schema
