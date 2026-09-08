#include "artwork/puppet_build_json.h"

#include <string>
#include <utility>

#include "artwork/layered_puppet.h"
#include "artwork/profile_silhouette.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

nlohmann::json PointJson(const ProfileControlPoint& point) {
  return nlohmann::json::array({point.x, point.y});
}

}  // namespace

std::string LayeredPuppetGeometryJson(const LayeredPuppet& puppet) {
  nlohmann::json joints = nlohmann::json::array();
  for (const ProfileControlPoint& joint : puppet.source_joints) joints.push_back(PointJson(joint));

  nlohmann::json bones = nlohmann::json::array();
  for (const ProfileControlBone& bone : puppet.bones) {
    bones.push_back(
        {{"start", bone.start_joint}, {"end", bone.end_joint}, {"may_stretch", bone.may_stretch}});
  }

  nlohmann::json parts = nlohmann::json::array();
  for (const LayeredPuppetPart& part : puppet.parts) {
    nlohmann::json vertices = nlohmann::json::array();
    for (const LayeredPuppetMeshVertex& vertex : part.mesh.vertices) {
      vertices.push_back(
          {{"point", PointJson(vertex.source)}, {"first_bone_weight", vertex.first_bone_weight}});
    }
    nlohmann::json triangles = nlohmann::json::array();
    for (const LayeredPuppetMeshTriangle& triangle : part.mesh.triangles) {
      triangles.push_back(triangle.vertices);
    }
    parts.push_back({
        {"name", part.name},
        {"bones", part.bone_indices},
        {"mesh", {{"vertices", std::move(vertices)}, {"triangles", std::move(triangles)}}},
    });
  }

  nlohmann::json poses = nlohmann::json::array();
  for (const LayeredPuppetPose& pose : puppet.poses) {
    nlohmann::json pose_joints = nlohmann::json::array();
    for (const ProfileControlPoint& joint : pose.joints) pose_joints.push_back(PointJson(joint));
    poses.push_back({
        {"name", pose.name},
        {"joints", std::move(pose_joints)},
        {"draw_order", pose.draw_order},
    });
  }

  const nlohmann::json payload = {
      {"width", puppet.width},
      {"height", puppet.height},
      {"source_joints", std::move(joints)},
      {"bones", std::move(bones)},
      {"parts", std::move(parts)},
      {"poses", std::move(poses)},
  };
  return payload.dump();
}

}  // namespace zebes
