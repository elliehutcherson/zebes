#include "artwork/layered_puppet_editor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "artwork/layered_puppet.h"
#include "common/image_io.h"
#include "gtest/gtest.h"
#include "tests/macros.h"

namespace zebes {
namespace {
constexpr char kDigest[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

RgbaImage TestImage() {
  RgbaImage image{
      .width = 16,
      .height = 16,
      .pixels = std::vector<uint8_t>(16 * 16 * 4, 0),
  };
  image.pixels[0] = 255;
  image.pixels[3] = 255;
  return image;
}

TEST(LayeredPuppetEditorTest, PreservesAuthoredBackToFrontOrderAndExportControl) {
  LayeredPuppet puppet{
      .width = 16,
      .height = 16,
      .source_joints = {{.x = 4, .y = 4}, {.x = 8, .y = 8}},
      .bones = {{.start_joint = 0, .end_joint = 1}},
      .parts =
          {
              {.name = "rear_leg",
               .bone_indices = {0},
               .artwork = TestImage(),
               .visible_artwork = TestImage()},
              {.name = "body_visible",
               .bone_indices = {0},
               .artwork = TestImage(),
               .visible_artwork = TestImage()},
              {.name = "front_arm",
               .bone_indices = {0},
               .artwork = TestImage(),
               .visible_artwork = TestImage()},
          },
      .poses = {{.name = "neutral",
                 .joints = {{.x = 4, .y = 4}, {.x = 8, .y = 8}},
                 .draw_order = {0, 1, 2}}},
  };
  const SkeletonRig reference_rig{
      .points = {{.name = "ear_l", .chain = "head"}, {.name = "ear_r", .chain = "head"}},
      .bones = {{.start = "ear_l", .end = "ear_r"}},
  };
  const SkeletonRigClip reference_clip{
      .id = "run",
      .name = "run",
      .fps = 8,
      .frames = {{.label = "neutral",
                  .pose = {{"ear_l", {.x = 4, .y = 4}}, {"ear_r", {.x = 8, .y = 8}}}}},
  };

  ASSERT_OK_AND_ASSIGN(
      const LayeredPuppetEditorStateContract contract,
      BuildLayeredPuppetEditorStateContract(puppet, kDigest, &reference_rig, &reference_clip));
  ASSERT_OK_AND_ASSIGN(
      const std::string html,
      RenderLayeredPuppetEditorHtml(puppet, 0, contract, &reference_rig, &reference_clip));

  EXPECT_NE(html.find("rear_leg"), std::string::npos);
  EXPECT_NE(html.find("body_visible"), std::string::npos);
  EXPECT_NE(html.find("front_arm"), std::string::npos);
  EXPECT_NE(html.find("\"draw_order\":[0,1,2]"), std::string::npos);
  EXPECT_NE(html.find("Export mangled PNG"), std::string::npos);
  EXPECT_NE(html.find("Export editor state JSON"), std::string::npos);
  EXPECT_NE(html.find("localStorage.setItem"), std::string::npos);
  EXPECT_NE(html.find("Calibrate source joints"), std::string::npos);
  EXPECT_NE(html.find("Define attachment mesh"), std::string::npos);
  EXPECT_NE(html.find("Mesh brush radius"), std::string::npos);
  EXPECT_NE(html.find("paintAttachment"), std::string::npos);
  EXPECT_NE(html.find("Paint mesh"), std::string::npos);
  EXPECT_NE(html.find("Erase mesh"), std::string::npos);
  EXPECT_NE(html.find("buildPaintMesh"), std::string::npos);
  EXPECT_NE(html.find("Use traced mesh"), std::string::npos);
  EXPECT_NE(html.find("no mesh"), std::string::npos);
  EXPECT_NE(html.find("Zoom"), std::string::npos);
  EXPECT_NE(html.find("painted_meshes"), std::string::npos);
  EXPECT_NE(html.find("source.png"), std::string::npos);
  EXPECT_NE(html.find("Show 23-point authoring skeleton"), std::string::npos);
  EXPECT_EQ(html.find("Show 13-joint deformation rig"), std::string::npos);
  EXPECT_NE(html.find("\"reference_rig\":{\"bones\""), std::string::npos);
  EXPECT_NE(html.find("Save to Repo"), std::string::npos);
  EXPECT_NE(html.find("Set anchor and regenerate"), std::string::npos);
  EXPECT_NE(html.find("Play 8 FPS"), std::string::npos);
  EXPECT_NE(html.find("/api/state"), std::string::npos);
  EXPECT_NE(html.find("pointermove"), std::string::npos);
}

}  // namespace
}  // namespace zebes
