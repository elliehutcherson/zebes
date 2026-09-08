#include "artwork/puppet_build_json.h"

#include <string>
#include <vector>

#include "artwork/layered_puppet.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

// A rigid body on one bone and a skinned arm on two, which is the whole shape
// of what the browser has to tell apart.
LayeredPuppet TwoPartPuppet() {
  LayeredPuppet puppet{.width = 32, .height = 32};
  puppet.source_joints = {{.x = 4, .y = 4}, {.x = 10, .y = 10}, {.x = 16, .y = 16}};
  puppet.bones = {{.start_joint = 0, .end_joint = 1}, {.start_joint = 1, .end_joint = 2}};
  puppet.parts.push_back({.name = "body", .bone_indices = {0}});
  LayeredPuppetPart arm{.name = "arm", .bone_indices = {0, 1}};
  arm.mesh.vertices = {{.source = {.x = 0, .y = 0}, .first_bone_weight = 1.0},
                       {.source = {.x = 4, .y = 0}, .first_bone_weight = 0.5},
                       {.source = {.x = 0, .y = 4}, .first_bone_weight = 0.0}};
  arm.mesh.triangles = {{.vertices = {0, 1, 2}}};
  puppet.parts.push_back(std::move(arm));
  puppet.poses.push_back({.name = "rest",
                          .joints = {{.x = 4, .y = 4}, {.x = 10, .y = 10}, {.x = 16, .y = 16}},
                          .draw_order = {0, 1}});
  puppet.poses.push_back({.name = "reach",
                          .joints = {{.x = 4, .y = 4}, {.x = 12, .y = 8}, {.x = 20, .y = 6}},
                          .draw_order = {1, 0}});
  return puppet;
}

TEST(PuppetBuildJsonTest, CarriesTheBindSkeletonAndEveryPose) {
  const nlohmann::json build = nlohmann::json::parse(LayeredPuppetGeometryJson(TwoPartPuppet()));

  EXPECT_EQ(build.at("width").get<int>(), 32);
  EXPECT_EQ(build.at("source_joints").size(), 3u);
  EXPECT_EQ(build.at("source_joints")[1].get<std::vector<double>>(), (std::vector<double>{10, 10}));
  ASSERT_EQ(build.at("bones").size(), 2u);
  EXPECT_EQ(build.at("bones")[1].at("start").get<int>(), 1);
  EXPECT_EQ(build.at("bones")[1].at("end").get<int>(), 2);

  ASSERT_EQ(build.at("poses").size(), 2u);
  EXPECT_EQ(build.at("poses")[1].at("name").get<std::string>(), "reach");
  EXPECT_EQ(build.at("poses")[1].at("joints")[2].get<std::vector<double>>(),
            (std::vector<double>{20, 6}));
  EXPECT_EQ(build.at("poses")[1].at("draw_order").get<std::vector<int>>(),
            (std::vector<int>{1, 0}));
}

TEST(PuppetBuildJsonTest, ARigidPartHasNoTrianglesAndASkinnedOneKeepsItsWeights) {
  const nlohmann::json build = nlohmann::json::parse(LayeredPuppetGeometryJson(TwoPartPuppet()));

  const nlohmann::json& body = build.at("parts")[0];
  EXPECT_EQ(body.at("name").get<std::string>(), "body");
  EXPECT_EQ(body.at("bones").get<std::vector<int>>(), (std::vector<int>{0}));
  EXPECT_TRUE(body.at("mesh").at("triangles").empty());

  const nlohmann::json& arm = build.at("parts")[1];
  EXPECT_EQ(arm.at("bones").get<std::vector<int>>(), (std::vector<int>{0, 1}));
  ASSERT_EQ(arm.at("mesh").at("vertices").size(), 3u);
  EXPECT_EQ(arm.at("mesh").at("vertices")[1].at("first_bone_weight").get<double>(), 0.5);
  EXPECT_EQ(arm.at("mesh").at("vertices")[1].at("point").get<std::vector<double>>(),
            (std::vector<double>{4, 0}));
  ASSERT_EQ(arm.at("mesh").at("triangles").size(), 1u);
  EXPECT_EQ(arm.at("mesh").at("triangles")[0].get<std::vector<int>>(), (std::vector<int>{0, 1, 2}));
}

TEST(PuppetBuildJsonTest, CarriesNoPixelsAndNoDocumentFields) {
  const std::string encoded = LayeredPuppetGeometryJson(TwoPartPuppet());
  // Artwork is fetched per part by name; putting pixels here would resend every
  // layer on every rebuild.
  EXPECT_EQ(encoded.find("artwork"), std::string::npos);
  EXPECT_EQ(encoded.find("source_image"), std::string::npos);
  EXPECT_EQ(encoded.find("guide_image"), std::string::npos);
}

}  // namespace
}  // namespace zebes
