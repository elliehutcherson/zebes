#include "artwork/layered_puppet_editor.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "artwork/layered_puppet.h"
#include "common/image_digest.h"
#include "common/status_macros.h"
#include "nlohmann/json.hpp"

namespace zebes {
namespace {

nlohmann::json PointJson(const ProfileControlPoint& point) {
  return nlohmann::json::array({point.x, point.y});
}

constexpr std::array<std::pair<std::string_view, size_t>, 13> kReferenceJointMapping{{
    {"elbow_r", 0},
    {"elbow_l", 1},
    {"toe_l", 2},
    {"toe_r", 3},
    {"paw_r", 4},
    {"paw_l", 5},
    {"head_top", 6},
    {"hip_c", 7},
    {"knee_l", 8},
    {"knee_r", 9},
    {"neck", 10},
    {"shoulder_r", 11},
    {"shoulder_l", 12},
}};

int ReferenceDeformationJoint(std::string_view name) {
  const auto found = std::find_if(kReferenceJointMapping.begin(), kReferenceJointMapping.end(),
                                  [name](const auto& mapping) { return mapping.first == name; });
  if (found == kReferenceJointMapping.end()) return -1;
  return static_cast<int>(found->second);
}

absl::Status ValidateReferenceRig(const LayeredPuppet& puppet, const SkeletonRig* rig,
                                  const SkeletonRigClip* clip) {
  if (rig == nullptr && clip == nullptr) return absl::OkStatus();
  if (rig == nullptr || clip == nullptr || clip->frames.size() != puppet.poses.size()) {
    return absl::InvalidArgumentError(
        "layered puppet editor reference rig and clip must match every puppet pose");
  }
  for (size_t frame_index = 0; frame_index < clip->frames.size(); ++frame_index) {
    if (clip->frames[frame_index].label != puppet.poses[frame_index].name) {
      return absl::InvalidArgumentError(
          "layered puppet editor reference rig frame order differs from puppet poses");
    }
    for (const SkeletonRigPoint& point : rig->points) {
      const int mapped_joint = ReferenceDeformationJoint(point.name);
      if (mapped_joint >= 0 && static_cast<size_t>(mapped_joint) >= puppet.source_joints.size()) {
        return absl::InvalidArgumentError(
            "layered puppet editor reference joint mapping is outside the deformation rig");
      }
      const auto joint = clip->frames[frame_index].pose.find(point.name);
      if (joint == clip->frames[frame_index].pose.end() || joint->second.x < 0.0 ||
          joint->second.y < 0.0 || joint->second.x >= puppet.width ||
          joint->second.y >= puppet.height) {
        return absl::InvalidArgumentError(
            "layered puppet editor reference joint is missing or outside the canvas");
      }
    }
  }
  return absl::OkStatus();
}

nlohmann::json ReferenceRigData(const SkeletonRig* rig, const SkeletonRigClip* clip) {
  if (rig == nullptr) return nullptr;
  absl::flat_hash_map<std::string, size_t> point_indices;
  nlohmann::json points = nlohmann::json::array();
  for (size_t index = 0; index < rig->points.size(); ++index) {
    point_indices.emplace(rig->points[index].name, index);
    const int deformation_joint = ReferenceDeformationJoint(rig->points[index].name);
    points.push_back({
        {"name", rig->points[index].name},
        {"chain", rig->points[index].chain},
        {"deformation_joint",
         deformation_joint >= 0 ? nlohmann::json(deformation_joint) : nlohmann::json(nullptr)},
    });
  }
  nlohmann::json bones = nlohmann::json::array();
  for (const SkeletonRigBone& bone : rig->bones) {
    bones.push_back({{"start", point_indices.at(bone.start)}, {"end", point_indices.at(bone.end)}});
  }
  nlohmann::json frames = nlohmann::json::array();
  for (const SkeletonRigFrame& frame : clip->frames) {
    nlohmann::json joints = nlohmann::json::array();
    for (const SkeletonRigPoint& point : rig->points) {
      const SkeletonRigJoint& joint = frame.pose.at(point.name);
      joints.push_back({joint.x, joint.y});
    }
    frames.push_back({{"name", frame.label}, {"joints", std::move(joints)}});
  }
  return {
      {"points", std::move(points)},
      {"bones", std::move(bones)},
      {"frames", std::move(frames)},
  };
}

nlohmann::json EditorData(const LayeredPuppet& puppet, const SkeletonRig* reference_rig,
                          const SkeletonRigClip* reference_clip) {
  nlohmann::json joints = nlohmann::json::array();
  for (const ProfileControlPoint& point : puppet.source_joints) joints.push_back(PointJson(point));

  nlohmann::json bones = nlohmann::json::array();
  for (const ProfileControlBone& bone : puppet.bones) {
    bones.push_back({{"start", bone.start_joint}, {"end", bone.end_joint}});
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
    for (const ProfileControlPoint& point : pose.joints) pose_joints.push_back(PointJson(point));
    poses.push_back({
        {"name", pose.name},
        {"joints", std::move(pose_joints)},
        {"draw_order", pose.draw_order},
    });
  }

  return {
      {"width", puppet.width},
      {"height", puppet.height},
      {"source_joints", std::move(joints)},
      {"bones", std::move(bones)},
      {"parts", std::move(parts)},
      {"poses", std::move(poses)},
      {"reference_rig", ReferenceRigData(reference_rig, reference_clip)},
  };
}

constexpr int kPaintMeshSpacing = 4;

nlohmann::json ContractData(const LayeredPuppetEditorStateContract& contract) {
  nlohmann::json counts = nlohmann::json::object();
  for (const auto& [name, count] : contract.part_triangle_counts) counts[name] = count;
  return {
      {"version", 1},
      {"source_rgba_digest", contract.source_rgba_digest},
      {"puppet_contract_digest", contract.puppet_contract_digest},
      {"frame_names", contract.frame_names},
      {"joint_count", contract.joint_count},
      {"part_triangle_counts", std::move(counts)},
  };
}

}  // namespace

absl::StatusOr<LayeredPuppetEditorStateContract> BuildLayeredPuppetEditorStateContract(
    const LayeredPuppet& puppet, std::string_view source_rgba_digest,
    const SkeletonRig* reference_rig, const SkeletonRigClip* reference_clip) {
  RETURN_IF_ERROR(ValidateLayeredPuppet(puppet));
  RETURN_IF_ERROR(ValidateReferenceRig(puppet, reference_rig, reference_clip));
  if (!IsLowercaseSha256Digest(source_rgba_digest)) {
    return absl::InvalidArgumentError("layered puppet editor source digest is invalid");
  }
  LayeredPuppetEditorStateContract contract{
      .source_rgba_digest = std::string(source_rgba_digest),
      .puppet_contract_digest =
          Sha256Digest(EditorData(puppet, reference_rig, reference_clip).dump()),
      .joint_count = puppet.source_joints.size(),
  };
  contract.frame_names.reserve(puppet.poses.size());
  const size_t columns =
      (static_cast<size_t>(puppet.width) + kPaintMeshSpacing - 1) / kPaintMeshSpacing;
  const size_t rows =
      (static_cast<size_t>(puppet.height) + kPaintMeshSpacing - 1) / kPaintMeshSpacing;
  for (const LayeredPuppetPose& pose : puppet.poses) contract.frame_names.push_back(pose.name);
  for (const LayeredPuppetPart& part : puppet.parts) {
    contract.part_triangle_counts.emplace(part.name,
                                          part.mesh.triangles.empty() ? 0 : columns * rows * 2);
  }
  return contract;
}

absl::StatusOr<std::string> RenderLayeredPuppetEditorHtml(
    const LayeredPuppet& puppet, size_t initial_pose_index,
    const LayeredPuppetEditorStateContract& contract, const SkeletonRig* reference_rig,
    const SkeletonRigClip* reference_clip) {
  ASSIGN_OR_RETURN(const LayeredPuppetEditorStateContract expected,
                   BuildLayeredPuppetEditorStateContract(puppet, contract.source_rgba_digest,
                                                         reference_rig, reference_clip));
  if (contract.puppet_contract_digest != expected.puppet_contract_digest ||
      contract.frame_names != expected.frame_names ||
      contract.joint_count != expected.joint_count ||
      contract.part_triangle_counts != expected.part_triangle_counts) {
    return absl::FailedPreconditionError(
        "layered puppet editor state contract does not match the puppet");
  }
  if (initial_pose_index >= puppet.poses.size()) {
    return absl::InvalidArgumentError("layered puppet editor initial pose is out of range");
  }

  std::string html = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Layered Puppet Pose Editor</title>
<style>
:root { color-scheme: dark; font-family: ui-sans-serif, system-ui, sans-serif; background: #15181d; color: #eef1f5; }
* { box-sizing: border-box; }
body { margin: 0; }
header { padding: 14px 20px; background: #222832; box-shadow: 0 3px 12px #0008; }
h1 { margin: 0 0 5px; font-size: 19px; }
p { margin: 0; color: #b9c0ca; }
main { display: grid; grid-template-columns: minmax(320px, 768px) minmax(260px, 380px); gap: 20px; align-items: start; padding: 20px; }
.viewport { width: min(75vw, 768px); height: min(80vh, 768px); overflow: auto; border-radius: 8px; background: #0f1216; box-shadow: 0 10px 30px #0008; }
.stage { position: relative; width: 768px; height: 768px; background: #fff; overflow: hidden; image-rendering: pixelated; }
canvas { position: absolute; inset: 0; width: 100%; height: 100%; image-rendering: pixelated; }
#skeleton { touch-action: none; cursor: crosshair; }
.panel { background: #222832; border: 1px solid #38414e; border-radius: 8px; padding: 15px; }
label { display: block; margin: 0 0 12px; color: #cbd2dc; font-size: 13px; }
select, button { width: 100%; margin-top: 5px; padding: 9px 10px; border: 1px solid #566171; border-radius: 5px; background: #303844; color: #fff; }
button { cursor: pointer; margin-bottom: 9px; }
button.primary { background: #246c49; border-color: #3b9a6e; }
.check { display: flex; gap: 8px; align-items: center; }
.check input { margin: 0; }
.status { margin: 12px 0; padding: 9px; border-radius: 5px; background: #171b21; font: 12px/1.4 ui-monospace, monospace; white-space: pre-wrap; }
.order { color: #9fa8b5; font-size: 12px; line-height: 1.5; overflow-wrap: anywhere; }
.mesh-buttons { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.frame-buttons { display: grid; grid-template-columns: 1fr 1.4fr 1fr; gap: 8px; }
.save-status { margin: 8px 0 12px; padding: 7px 9px; border-radius: 5px; background: #171b21; color: #b9c0ca; font-size: 12px; }
.save-status.dirty { color: #ffd18a; }
.save-status.saved { color: #83e1ad; }
.mesh-note { margin: 0 0 12px; color: #9fa8b5; font-size: 12px; line-height: 1.4; }
input[type="range"] { width: 100%; margin-top: 7px; }
.value { float: right; color: #fff; font-variant-numeric: tabular-nums; }
@media (max-width: 850px) { main { grid-template-columns: 1fr; } .viewport { width: 100%; } }
</style>
</head>
<body>
<header>
<h1>Layered Puppet Pose Editor</h1>
<p>Drag skeleton joints. Legs and rear arm stay behind the coat; the near arm draws above it. Export the intentionally rough composite for cleanup.</p>
</header>
<main>
<div class="viewport">
<div class="stage" id="stage">
<canvas id="composite"></canvas>
<canvas id="skeleton"></canvas>
</div>
</div>
<aside class="panel">
<label>Current frame<select id="pose"></select></label>
<div class="frame-buttons">
<button id="previous-frame" type="button">Previous</button>
<button id="play" type="button">Play 8 FPS</button>
<button id="next-frame" type="button">Next</button>
</div>
<label>Source corresponds to frame<select id="anchor-frame"></select></label>
<button id="set-anchor" type="button">Set anchor and regenerate</button>
<label>Edit mode
<select id="mode">
<option value="pose">Move pose joints — deforms image</option>
<option value="source">Calibrate source joints — image stays fixed</option>
<option value="mesh">Define attachment mesh</option>
</select>
</label>
<label>Attachment part<select id="mesh-part"></select></label>
<label>Mesh brush action
<select id="brush-action">
<option value="erase">Erase mesh</option>
<option value="paint">Paint mesh</option>
</select>
</label>
<label>Mesh brush radius <span class="value" id="brush-size-value">8 px</span>
<input id="brush-size" type="range" min="2" max="24" step="1" value="8">
</label>
<div class="mesh-buttons">
<button id="restore-mesh" type="button">Use traced mesh</button>
<button id="clear-mesh" type="button">Clear mesh</button>
</div>
<p class="mesh-note"><strong>Green:</strong> painted mesh that moves with the selected bone chain. <strong>No overlay:</strong> no mesh, so those source pixels stay on the static body. Paint or erase anywhere on the source image.</p>
<label class="check" id="reference-toggle"><input id="show-reference" type="checkbox" checked> Show 23-point authoring skeleton</label>
<label>Zoom <span class="value" id="zoom-value">3×</span>
<input id="zoom" type="range" min="1" max="6" step="0.25" value="3">
</label>
<button id="reset" type="button">Reset selected pose</button>
<button id="reset-all" type="button">Clear all saved edits</button>
<div class="mesh-buttons">
<button id="save-repo" class="primary" type="button">Save to Repo</button>
<button id="revert-repo" type="button">Revert from Repo</button>
</div>
<div id="save-status" class="save-status">Loading repository state…</div>
<button id="export-data" type="button">Export editor state JSON</button>
<button id="export" class="primary" type="button">Export mangled PNG</button>
<div id="status" class="status">Loading layers…</div>
<div class="order"><strong>Back-to-front layers</strong><br><span id="order"></span></div>
</aside>
</main>
<script>
const puppet = )html";
  absl::StrAppend(&html, EditorData(puppet, reference_rig, reference_clip).dump(),
                  ";\nconst stateContract = ", ContractData(contract).dump(),
                  ";\nconst initialPose = ", initial_pose_index,
                  R"html(;
const composite = document.querySelector('#composite');
const skeleton = document.querySelector('#skeleton');
const stage = document.querySelector('#stage');
const compositeContext = composite.getContext('2d');
const bodyCanvas = document.createElement('canvas');
const bodyContext = bodyCanvas.getContext('2d');
const skeletonContext = skeleton.getContext('2d');
const poseSelect = document.querySelector('#pose');
const anchorSelect = document.querySelector('#anchor-frame');
const modeSelect = document.querySelector('#mode');
const meshPartSelect = document.querySelector('#mesh-part');
const brushAction = document.querySelector('#brush-action');
const brushSize = document.querySelector('#brush-size');
const brushSizeValue = document.querySelector('#brush-size-value');
const zoom = document.querySelector('#zoom');
const zoomValue = document.querySelector('#zoom-value');
const showReference = document.querySelector('#show-reference');
const status = document.querySelector('#status');
const saveStatus = document.querySelector('#save-status');
const order = document.querySelector('#order');
composite.width = skeleton.width = puppet.width;
composite.height = skeleton.height = puppet.height;
bodyCanvas.width = puppet.width;
bodyCanvas.height = puppet.height;
compositeContext.imageSmoothingEnabled = false;
skeletonContext.imageSmoothingEnabled = false;
bodyContext.imageSmoothingEnabled = false;
let activeJoint = null;
let painting = false;
let brushPoint = null;
let currentPose = initialPose;
let playingTimer = null;
let targetJoints = [];
let images = [];
let sourceImage = null;
let sourceJoints = cloneJoints(puppet.source_joints);
let anchorFrame = stateContract.frame_names[initialPose];
let frameOverrides = puppet.poses.map(() => zeroJoints());
let authoredFrames = new Set();
let editedPoses = [];
let paintMeshes = puppet.parts.map(buildPaintMesh);
const storageKey = `zebes-layered-puppet-editor:${location.pathname}`;

function cloneJoints(joints) { return joints.map(point => [point[0], point[1]]); }
function zeroJoints() { return puppet.source_joints.map(() => [0, 0]); }
function validJoints(joints) {
  return Array.isArray(joints) && joints.length === stateContract.joint_count &&
      joints.every(point => Array.isArray(point) && point.length === 2 &&
          point.every(Number.isFinite));
}
function validRepoState(saved) {
  if (saved === null || saved.version !== 1 ||
      saved.source_rgba_digest !== stateContract.source_rgba_digest ||
      saved.puppet_contract_digest !== stateContract.puppet_contract_digest ||
      !stateContract.frame_names.includes(saved.anchor_frame) ||
      !validJoints(saved.source_joints) || typeof saved.painted_meshes !== 'object' ||
      typeof saved.frame_overrides !== 'object') return false;
  for (const [name, indices] of Object.entries(saved.painted_meshes)) {
    const count = stateContract.part_triangle_counts[name];
    if (count === undefined || !Array.isArray(indices) ||
        indices.some((index, position) => !Number.isInteger(index) || index < 0 ||
            index >= count || (position > 0 && index <= indices[position - 1]))) return false;
  }
  return Object.entries(saved.frame_overrides).every(
      ([name, offsets]) => stateContract.frame_names.includes(name) && validJoints(offsets));
}
function setSaveStatus(message, stateClass) {
  saveStatus.textContent = message;
  saveStatus.className = `save-status ${stateClass}`;
}
function regeneratePoses() {
  const anchorIndex = stateContract.frame_names.indexOf(anchorFrame);
  editedPoses = puppet.poses.map((pose, frameIndex) => pose.joints.map((joint, jointIndex) => [
    sourceJoints[jointIndex][0] + joint[0] - puppet.poses[anchorIndex].joints[jointIndex][0] +
        frameOverrides[frameIndex][jointIndex][0],
    sourceJoints[jointIndex][1] + joint[1] - puppet.poses[anchorIndex].joints[jointIndex][1] +
        frameOverrides[frameIndex][jointIndex][1]
  ]));
  targetJoints = editedPoses[currentPose];
}
function updateCurrentOverride() {
  const anchorIndex = stateContract.frame_names.indexOf(anchorFrame);
  frameOverrides[currentPose] = targetJoints.map((joint, jointIndex) => [
    joint[0] - sourceJoints[jointIndex][0] - puppet.poses[currentPose].joints[jointIndex][0] +
        puppet.poses[anchorIndex].joints[jointIndex][0],
    joint[1] - sourceJoints[jointIndex][1] - puppet.poses[currentPose].joints[jointIndex][1] +
        puppet.poses[anchorIndex].joints[jointIndex][1]
  ]);
  authoredFrames.add(puppet.poses[currentPose].name);
}
function currentState() {
  const paintedMeshes = {};
  puppet.parts.forEach((part, partIndex) => {
    if (paintMeshes[partIndex].triangles.length === 0) return;
    paintedMeshes[part.name] = paintMeshes[partIndex].active.flatMap(
        (active, triangleIndex) => active ? [triangleIndex] : []);
  });
  const overrides = {};
  authoredFrames.forEach(name => {
    const frameIndex = stateContract.frame_names.indexOf(name);
    overrides[name] = frameOverrides[frameIndex];
  });
  return {
    version: 1,
    source_rgba_digest: stateContract.source_rgba_digest,
    puppet_contract_digest: stateContract.puppet_contract_digest,
    anchor_frame: anchorFrame,
    source_joints: sourceJoints,
    painted_meshes: paintedMeshes,
    frame_overrides: overrides
  };
}
function applyState(saved) {
  if (!validRepoState(saved)) throw new Error('saved state does not match this editor');
  sourceJoints = cloneJoints(saved.source_joints);
  anchorFrame = saved.anchor_frame;
  authoredFrames = new Set(Object.keys(saved.frame_overrides));
  frameOverrides = puppet.poses.map(
      pose => cloneJoints(saved.frame_overrides[pose.name] ?? zeroJoints()));
  paintMeshes = puppet.parts.map(buildPaintMesh);
  for (const [name, indices] of Object.entries(saved.painted_meshes)) {
    const partIndex = puppet.parts.findIndex(part => part.name === name);
    paintMeshes[partIndex].active.fill(false);
    indices.forEach(index => { paintMeshes[partIndex].active[index] = true; });
  }
  anchorSelect.value = anchorFrame;
  regeneratePoses();
}
function saveDraft() {
  localStorage.setItem(storageKey, JSON.stringify(currentState()));
  setSaveStatus('Unsaved local draft', 'dirty');
}

function distanceToSegment(point, start, end) {
  const dx = end[0] - start[0];
  const dy = end[1] - start[1];
  const lengthSquared = dx * dx + dy * dy;
  const projection = lengthSquared > 0
      ? Math.max(0, Math.min(1, ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) /
          lengthSquared))
      : 0;
  return Math.hypot(point[0] - (start[0] + projection * dx),
                    point[1] - (start[1] + projection * dy));
}
function meshWeight(part, point) {
  if (part.bones.length < 2) return 1;
  const first = puppet.bones[part.bones[0]];
  const second = puppet.bones[part.bones[1]];
  const firstDistance = distanceToSegment(
      point, puppet.source_joints[first.start], puppet.source_joints[first.end]);
  const secondDistance = distanceToSegment(
      point, puppet.source_joints[second.start], puppet.source_joints[second.end]);
  const total = firstDistance + secondDistance;
  return total > 0 ? secondDistance / total : 0.5;
}
function buildPaintMesh(part) {
  if (part.mesh.triangles.length === 0) {
    return {vertices: [], triangles: [], seed: [], active: []};
  }
  const spacing = 4;
  const columns = Math.ceil(puppet.width / spacing);
  const rows = Math.ceil(puppet.height / spacing);
  const vertices = [];
  for (let row = 0; row <= rows; ++row) {
    for (let column = 0; column <= columns; ++column) {
      const point = [Math.min(column * spacing, puppet.width),
                     Math.min(row * spacing, puppet.height)];
      vertices.push({point, first_bone_weight: meshWeight(part, point)});
    }
  }
  const triangles = [];
  for (let row = 0; row < rows; ++row) {
    for (let column = 0; column < columns; ++column) {
      const topLeft = row * (columns + 1) + column;
      const topRight = topLeft + 1;
      const bottomLeft = topLeft + columns + 1;
      const bottomRight = bottomLeft + 1;
      triangles.push([topLeft, topRight, bottomRight]);
      triangles.push([topLeft, bottomRight, bottomLeft]);
    }
  }
  const originalTriangles = part.mesh.triangles.map(
      triangle => triangle.map(index => part.mesh.vertices[index].point));
  const seed = triangles.map(triangle => {
    const points = triangle.map(index => vertices[index].point);
    const center = [(points[0][0] + points[1][0] + points[2][0]) / 3,
                    (points[0][1] + points[1][1] + points[2][1]) / 3];
    return originalTriangles.some(original => pointInTriangle(center, original));
  });
  return {vertices, triangles, seed, active: [...seed]};
}
function boneTransform(boneIndex) {
  const bone = puppet.bones[boneIndex];
  const sourceStart = sourceJoints[bone.start];
  const sourceEnd = sourceJoints[bone.end];
  const targetStart = targetJoints[bone.start];
  const targetEnd = targetJoints[bone.end];
  const sourceDx = sourceEnd[0] - sourceStart[0];
  const sourceDy = sourceEnd[1] - sourceStart[1];
  const targetDx = targetEnd[0] - targetStart[0];
  const targetDy = targetEnd[1] - targetStart[1];
  const sourceLength = Math.hypot(sourceDx, sourceDy);
  const targetLength = Math.hypot(targetDx, targetDy);
  const angle = Math.atan2(targetDy, targetDx) - Math.atan2(sourceDy, sourceDx);
  return { sourceStart, targetStart, scale: sourceLength > 0 ? targetLength / sourceLength : 1,
           cosine: Math.cos(angle), sine: Math.sin(angle) };
}
function transformPoint(point, transform) {
  const x = point[0] - transform.sourceStart[0];
  const y = point[1] - transform.sourceStart[1];
  return [transform.targetStart[0] + transform.scale * (transform.cosine * x - transform.sine * y),
          transform.targetStart[1] + transform.scale * (transform.sine * x + transform.cosine * y)];
}
function deformedVertices(part, mesh) {
  const first = boneTransform(part.bones[0]);
  const second = part.bones.length === 2 ? boneTransform(part.bones[1]) : first;
  return mesh.vertices.map(vertex => {
    const firstPoint = transformPoint(vertex.point, first);
    const secondPoint = transformPoint(vertex.point, second);
    const weight = vertex.first_bone_weight;
    return [firstPoint[0] * weight + secondPoint[0] * (1 - weight),
            firstPoint[1] * weight + secondPoint[1] * (1 - weight)];
  });
}
function affine(source, target) {
  const denominator = source[0][0] * (source[1][1] - source[2][1]) +
      source[1][0] * (source[2][1] - source[0][1]) +
      source[2][0] * (source[0][1] - source[1][1]);
  if (Math.abs(denominator) < 0.0001) return null;
  const coefficients = values => ({
    x: (values[0] * (source[1][1] - source[2][1]) + values[1] * (source[2][1] - source[0][1]) + values[2] * (source[0][1] - source[1][1])) / denominator,
    y: (values[0] * (source[2][0] - source[1][0]) + values[1] * (source[0][0] - source[2][0]) + values[2] * (source[1][0] - source[0][0])) / denominator,
    offset: (values[0] * (source[1][0] * source[2][1] - source[2][0] * source[1][1]) + values[1] * (source[2][0] * source[0][1] - source[0][0] * source[2][1]) + values[2] * (source[0][0] * source[1][1] - source[1][0] * source[0][1])) / denominator
  });
  const horizontal = coefficients(target.map(point => point[0]));
  const vertical = coefficients(target.map(point => point[1]));
  return [horizontal.x, vertical.x, horizontal.y, vertical.y, horizontal.offset, vertical.offset];
}
function expandedTriangle(points) {
  const center = [(points[0][0] + points[1][0] + points[2][0]) / 3,
                  (points[0][1] + points[1][1] + points[2][1]) / 3];
  return points.map(point => {
    const x = point[0] - center[0];
    const y = point[1] - center[1];
    const length = Math.hypot(x, y);
    return length > 0 ? [point[0] + x * 0.65 / length, point[1] + y * 0.65 / length] : point;
  });
}
function drawTriangle(context, image, source, target) {
  const expanded = expandedTriangle(target);
  const matrix = affine(source, expanded);
  if (matrix === null) return;
  context.save();
  context.beginPath();
  context.moveTo(expanded[0][0], expanded[0][1]);
  context.lineTo(expanded[1][0], expanded[1][1]);
  context.lineTo(expanded[2][0], expanded[2][1]);
  context.closePath();
  context.clip();
  context.transform(...matrix);
  context.drawImage(image, 0, 0);
  context.restore();
}
function drawRigidImage(context, image, boneIndex) {
  const transform = boneTransform(boneIndex);
  const a = transform.scale * transform.cosine;
  const b = transform.scale * transform.sine;
  const c = -transform.scale * transform.sine;
  const d = transform.scale * transform.cosine;
  const e = transform.targetStart[0] - a * transform.sourceStart[0] - c * transform.sourceStart[1];
  const f = transform.targetStart[1] - b * transform.sourceStart[0] - d * transform.sourceStart[1];
  context.save();
  context.transform(a, b, c, d, e, f);
  context.drawImage(image, 0, 0);
  context.restore();
}
function clearTriangle(context, points) {
  const expanded = expandedTriangle(points);
  context.beginPath();
  context.moveTo(...expanded[0]);
  context.lineTo(...expanded[1]);
  context.lineTo(...expanded[2]);
  context.closePath();
  context.fill();
}
function buildStaticBody(bodyIndex) {
  bodyContext.clearRect(0, 0, puppet.width, puppet.height);
  drawRigidImage(bodyContext, sourceImage, puppet.parts[bodyIndex].bones[0]);
  const bodyTransform = boneTransform(puppet.parts[bodyIndex].bones[0]);
  bodyContext.save();
  bodyContext.globalCompositeOperation = 'destination-out';
  bodyContext.fillStyle = '#fff';
  paintMeshes.forEach(mesh => {
    mesh.triangles.forEach((triangle, triangleIndex) => {
      if (!mesh.active[triangleIndex]) return;
      const points = triangle.map(index =>
          transformPoint(mesh.vertices[index].point, bodyTransform));
      clearTriangle(bodyContext, points);
    });
  });
  bodyContext.restore();
}
function drawPart(partIndex) {
  const part = puppet.parts[partIndex];
  const mesh = paintMeshes[partIndex];
  if (mesh.triangles.length === 0) {
    drawRigidImage(compositeContext, images[partIndex], part.bones[0]);
    return;
  }
  const targets = deformedVertices(part, mesh);
  mesh.triangles.forEach((triangle, triangleIndex) => {
    if (!mesh.active[triangleIndex]) return;
    drawTriangle(compositeContext, sourceImage,
                 triangle.map(index => mesh.vertices[index].point),
                 triangle.map(index => targets[index]));
  });
}
function drawAttachmentMesh() {
  if (modeSelect.value !== 'mesh') return;
  const partIndex = Number(meshPartSelect.value);
  const mesh = paintMeshes[partIndex];
  mesh.triangles.forEach((triangle, triangleIndex) => {
    if (!mesh.active[triangleIndex]) return;
    const points = triangle.map(index => mesh.vertices[index].point);
    skeletonContext.beginPath();
    skeletonContext.moveTo(...points[0]);
    skeletonContext.lineTo(...points[1]);
    skeletonContext.lineTo(...points[2]);
    skeletonContext.closePath();
    skeletonContext.fillStyle = '#35d07f33';
    skeletonContext.strokeStyle = '#35d07f';
    skeletonContext.lineWidth = 0.65;
    skeletonContext.fill();
    skeletonContext.stroke();
  });
  if (brushPoint === null) return;
  skeletonContext.beginPath();
  skeletonContext.arc(brushPoint[0], brushPoint[1], Number(brushSize.value), 0, Math.PI * 2);
  skeletonContext.strokeStyle = brushAction.value === 'paint' ? '#35d07f' : '#f05b66';
  skeletonContext.lineWidth = 1.5;
  skeletonContext.stroke();
}
function referenceColor(chain) {
  return {head:'#e66ee6', spine:'#eb4637', arm_l:'#faa532', arm_r:'#3ca5fa',
          leg_l:'#46c864', leg_r:'#a582f5', tail:'#beaa6e'}[chain] ?? '#b7bec8';
}
function referenceJointsForDisplay() {
  if (puppet.reference_rig === null) return [];
  const frame = puppet.reference_rig.frames[currentPose];
  const joints = cloneJoints(frame.joints);
  const useSource = modeSelect.value === 'source';
  const useAuthoredPose =
      modeSelect.value === 'pose' &&
      (authoredFrames.has(puppet.poses[currentPose].name) || activeJoint !== null);
  if (!useSource && !useAuthoredPose) return joints;
  frame.joints.forEach((_, index) => {
    const deformationJoint = puppet.reference_rig.points[index].deformation_joint;
    if (deformationJoint === null) return;
    joints[index] = useSource ? sourceJoints[deformationJoint] : targetJoints[deformationJoint];
  });
  return joints;
}
function drawReferenceSkeleton() {
  if (!showReference.checked || puppet.reference_rig === null) return;
  const reference = puppet.reference_rig;
  const joints = referenceJointsForDisplay();
  skeletonContext.save();
  skeletonContext.globalAlpha = 0.9;
  skeletonContext.lineWidth = 1.5;
  for (const bone of reference.bones) {
    skeletonContext.beginPath();
    skeletonContext.moveTo(...joints[bone.start]);
    skeletonContext.lineTo(...joints[bone.end]);
    skeletonContext.strokeStyle = referenceColor(reference.points[bone.end].chain);
    skeletonContext.stroke();
  }
  joints.forEach((point, index) => {
    const deformationJoint = reference.points[index].deformation_joint;
    skeletonContext.fillStyle = referenceColor(reference.points[index].chain);
    if (deformationJoint === null) {
      skeletonContext.fillRect(point[0] - 2, point[1] - 2, 4, 4);
      return;
    }
    skeletonContext.beginPath();
    skeletonContext.arc(point[0], point[1], deformationJoint === activeJoint ? 4 : 3, 0,
                        Math.PI * 2);
    skeletonContext.fill();
    skeletonContext.strokeStyle = '#17202a';
    skeletonContext.stroke();
  });
  skeletonContext.restore();
}
function drawSkeleton() {
  skeletonContext.clearRect(0, 0, puppet.width, puppet.height);
  drawReferenceSkeleton();
  drawAttachmentMesh();
}
function render() {
  compositeContext.clearRect(0, 0, puppet.width, puppet.height);
  if (modeSelect.value !== 'pose') {
    compositeContext.drawImage(sourceImage, 0, 0);
  } else {
    const bodyIndex = puppet.parts.findIndex(part => part.name === 'body_visible');
    if (bodyIndex >= 0) buildStaticBody(bodyIndex);
    for (const partIndex of puppet.poses[currentPose].draw_order) {
      if (partIndex === bodyIndex) {
        compositeContext.drawImage(bodyCanvas, 0, 0);
      } else {
        drawPart(partIndex);
      }
    }
  }
  drawSkeleton();
  const modeText = modeSelect.options[modeSelect.selectedIndex].textContent;
  const frameName = puppet.poses[currentPose].name;
  const frameState = authoredFrames.has(frameName) ? 'Authored' : 'Canonical';
  const anchorState = frameName === anchorFrame ? ' · Anchor' : '';
  status.textContent = `${frameName} · ${frameState}${anchorState} · ${modeText}`;
  order.textContent = puppet.poses[currentPose].draw_order.map(index => puppet.parts[index].name).join(' → ');
}
function selectPose(index) {
  currentPose = index;
  poseSelect.value = String(index);
  targetJoints = editedPoses[index];
  activeJoint = null;
  render();
}
function pointerPoint(event) {
  const bounds = skeleton.getBoundingClientRect();
  return [(event.clientX - bounds.left) * puppet.width / bounds.width,
          (event.clientY - bounds.top) * puppet.height / bounds.height];
}
function pointInTriangle(point, triangle) {
  const [a, b, c] = triangle;
  const area = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
  if (Math.abs(area) < 0.0001) return false;
  const first = ((b[1] - c[1]) * (point[0] - c[0]) +
                 (c[0] - b[0]) * (point[1] - c[1])) / area;
  const second = ((c[1] - a[1]) * (point[0] - c[0]) +
                  (a[0] - c[0]) * (point[1] - c[1])) / area;
  const third = 1 - first - second;
  return first >= 0 && second >= 0 && third >= 0;
}
function paintAttachment(point) {
  const partIndex = Number(meshPartSelect.value);
  const mesh = paintMeshes[partIndex];
  const radius = Number(brushSize.value);
  const desired = brushAction.value === 'paint';
  let changed = false;
  mesh.triangles.forEach((triangle, triangleIndex) => {
    const points = triangle.map(index => mesh.vertices[index].point);
    const center = [(points[0][0] + points[1][0] + points[2][0]) / 3,
                    (points[0][1] + points[1][1] + points[2][1]) / 3];
    if (Math.hypot(center[0] - point[0], center[1] - point[1]) > radius &&
        !pointInTriangle(point, points)) return;
    if (mesh.active[triangleIndex] === desired) return;
    mesh.active[triangleIndex] = desired;
    changed = true;
  });
  if (changed) render();
}
skeleton.addEventListener('pointerdown', event => {
  const point = pointerPoint(event);
  if (modeSelect.value === 'mesh') {
    painting = true;
    brushPoint = point;
    skeleton.setPointerCapture(event.pointerId);
    paintAttachment(point);
    drawSkeleton();
    return;
  }
  let distance = 12;
  activeJoint = null;
  if (puppet.reference_rig === null) {
    const joints = modeSelect.value === 'source' ? sourceJoints : targetJoints;
    joints.forEach((joint, index) => {
      const candidate = Math.hypot(joint[0] - point[0], joint[1] - point[1]);
      if (candidate < distance) { distance = candidate; activeJoint = index; }
    });
  } else {
    const joints = referenceJointsForDisplay();
    puppet.reference_rig.points.forEach((referencePoint, index) => {
      if (referencePoint.deformation_joint === null) return;
      const candidate = Math.hypot(joints[index][0] - point[0], joints[index][1] - point[1]);
      if (candidate < distance) {
        distance = candidate;
        activeJoint = referencePoint.deformation_joint;
      }
    });
  }
  if (activeJoint !== null) skeleton.setPointerCapture(event.pointerId);
  drawSkeleton();
});
skeleton.addEventListener('pointermove', event => {
  const point = pointerPoint(event);
  if (modeSelect.value === 'mesh') {
    brushPoint = point;
    if (painting) paintAttachment(point);
    drawSkeleton();
    return;
  }
  if (activeJoint === null) return;
  if (modeSelect.value === 'source') {
    sourceJoints[activeJoint] = point;
  } else {
    targetJoints[activeJoint] = point;
  }
  render();
});
skeleton.addEventListener('pointerleave', () => {
  if (painting) return;
  brushPoint = null;
  drawSkeleton();
});
skeleton.addEventListener('pointerup', event => {
  const changed = activeJoint !== null || painting;
  if (changed) skeleton.releasePointerCapture(event.pointerId);
  if (activeJoint !== null && modeSelect.value === 'pose') updateCurrentOverride();
  if (activeJoint !== null && modeSelect.value === 'source') regeneratePoses();
  activeJoint = null;
  painting = false;
  if (changed) {
    saveDraft();
    render();
  } else {
    drawSkeleton();
  }
});
showReference.addEventListener('change', drawSkeleton);
poseSelect.addEventListener('change', () => {
  stopPlayback();
  selectPose(Number(poseSelect.value));
});
modeSelect.addEventListener('change', () => {
  activeJoint = null;
  painting = false;
  brushPoint = null;
  if (modeSelect.value === 'source') {
    selectPose(stateContract.frame_names.indexOf(anchorFrame));
  } else {
    render();
  }
});
meshPartSelect.addEventListener('change', render);
brushAction.addEventListener('change', drawSkeleton);
brushSize.addEventListener('input', () => {
  brushSizeValue.textContent = `${brushSize.value} px`;
  drawSkeleton();
});
function updateZoom() {
  const scale = Number(zoom.value);
  stage.style.width = `${puppet.width * scale}px`;
  stage.style.height = `${puppet.height * scale}px`;
  zoomValue.textContent = `${scale}×`;
}
zoom.addEventListener('input', updateZoom);
document.querySelector('#restore-mesh').addEventListener('click', () => {
  const mesh = paintMeshes[Number(meshPartSelect.value)];
  mesh.active = [...mesh.seed];
  saveDraft();
  render();
});
document.querySelector('#clear-mesh').addEventListener('click', () => {
  paintMeshes[Number(meshPartSelect.value)].active.fill(false);
  saveDraft();
  render();
});
document.querySelector('#reset').addEventListener('click', () => {
  frameOverrides[currentPose] = zeroJoints();
  authoredFrames.delete(puppet.poses[currentPose].name);
  regeneratePoses();
  saveDraft();
  selectPose(currentPose);
});
document.querySelector('#reset-all').addEventListener('click', () => {
  if (!window.confirm('Clear saved source joints, painted meshes, and pose adjustments?')) return;
  sourceJoints = cloneJoints(puppet.source_joints);
  anchorFrame = stateContract.frame_names[initialPose];
  frameOverrides = puppet.poses.map(() => zeroJoints());
  authoredFrames.clear();
  paintMeshes = puppet.parts.map(buildPaintMesh);
  anchorSelect.value = anchorFrame;
  regeneratePoses();
  saveDraft();
  selectPose(currentPose);
});
function stopPlayback() {
  if (playingTimer !== null) window.clearInterval(playingTimer);
  playingTimer = null;
  document.querySelector('#play').textContent = 'Play 8 FPS';
}
function moveFrame(delta) {
  selectPose((currentPose + delta + puppet.poses.length) % puppet.poses.length);
}
document.querySelector('#previous-frame').addEventListener('click', () => {
  stopPlayback();
  moveFrame(-1);
});
document.querySelector('#next-frame').addEventListener('click', () => {
  stopPlayback();
  moveFrame(1);
});
document.querySelector('#play').addEventListener('click', () => {
  if (playingTimer !== null) {
    stopPlayback();
    return;
  }
  document.querySelector('#play').textContent = 'Pause';
  playingTimer = window.setInterval(() => moveFrame(1), 125);
});
document.addEventListener('keydown', event => {
  if (['INPUT', 'SELECT', 'TEXTAREA'].includes(event.target.tagName)) return;
  if (event.key === 'ArrowLeft') {
    stopPlayback();
    moveFrame(-1);
  } else if (event.key === 'ArrowRight') {
    stopPlayback();
    moveFrame(1);
  }
});
document.querySelector('#set-anchor').addEventListener('click', () => {
  const hasOverrides = frameOverrides.some(
      offsets => offsets.some(point => point[0] !== 0 || point[1] !== 0));
  if (hasOverrides &&
      !window.confirm('Changing the anchor clears every per-frame correction. Continue?')) return;
  anchorFrame = anchorSelect.value;
  frameOverrides = puppet.poses.map(() => zeroJoints());
  authoredFrames.clear();
  regeneratePoses();
  saveDraft();
  selectPose(currentPose);
});
async function fetchRepositoryState() {
  const response = await fetch('/api/state', {cache: 'no-store'});
  if (!response.ok) throw new Error(await response.text());
  const saved = await response.json();
  if (!validRepoState(saved)) throw new Error('repository state does not match this editor');
  return saved;
}
async function loadRepositoryState() {
  try {
    const repository = await fetchRepositoryState();
    let localDraft = null;
    try {
      localDraft = JSON.parse(localStorage.getItem(storageKey));
    } catch (_) {
      localStorage.removeItem(storageKey);
    }
    if (validRepoState(localDraft)) {
      applyState(localDraft);
      setSaveStatus('Unsaved local draft', 'dirty');
    } else {
      applyState(repository);
      localStorage.removeItem(storageKey);
      setSaveStatus('Loaded from repo', 'saved');
    }
    selectPose(currentPose);
  } catch (error) {
    regeneratePoses();
    selectPose(currentPose);
    setSaveStatus(`Repository state unavailable: ${error.message}`, 'dirty');
  }
}
document.querySelector('#save-repo').addEventListener('click', async () => {
  try {
    const response = await fetch('/api/state', {
      method: 'PUT',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(currentState())
    });
    if (!response.ok) throw new Error(await response.text());
    const saved = await response.json();
    applyState(saved);
    localStorage.removeItem(storageKey);
    selectPose(currentPose);
    setSaveStatus('Saved to repo', 'saved');
  } catch (error) {
    setSaveStatus(`Save failed: ${error.message}`, 'dirty');
  }
});
document.querySelector('#revert-repo').addEventListener('click', async () => {
  if (!window.confirm('Discard the local draft and reload repository state?')) return;
  try {
    const saved = await fetchRepositoryState();
    applyState(saved);
    localStorage.removeItem(storageKey);
    selectPose(currentPose);
    setSaveStatus('Reverted to repo', 'saved');
  } catch (error) {
    setSaveStatus(`Revert failed: ${error.message}`, 'dirty');
  }
});
function download(blob, name) {
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(link.href), 0);
}
document.querySelector('#export-data').addEventListener('click', () => {
  download(new Blob([JSON.stringify(currentState(), null, 2)], {type: 'application/json'}),
           'layered-puppet-state.json');
});
document.querySelector('#export').addEventListener('click', () => {
  composite.toBlob(blob => download(blob, `mangled-${puppet.poses[currentPose].name}.png`),
                   'image/png');
});
if (puppet.reference_rig === null) {
  document.querySelector('#reference-toggle').hidden = true;
  showReference.checked = false;
}
puppet.poses.forEach((pose, index) => {
  const option = document.createElement('option');
  option.value = index;
  option.textContent = pose.name;
  poseSelect.append(option);
  const anchorOption = document.createElement('option');
  anchorOption.value = pose.name;
  anchorOption.textContent = pose.name;
  anchorSelect.append(anchorOption);
});
puppet.parts.forEach((part, index) => {
  if (part.mesh.triangles.length === 0) return;
  const option = document.createElement('option');
  option.value = index;
  option.textContent = part.name;
  meshPartSelect.append(option);
});
updateZoom();
function loadImage(path) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error(`could not load ${path}`));
    image.src = path;
  });
}
Promise.all([loadImage('source.png'),
             ...puppet.parts.map(part => loadImage(`parts/${encodeURIComponent(part.name)}.png`))])
  .then(async loaded => {
    sourceImage = loaded[0];
    images = loaded.slice(1);
    anchorSelect.value = anchorFrame;
    regeneratePoses();
    await loadRepositoryState();
  })
  .catch(error => { status.textContent = error.message; });
</script>
</body>
</html>
)html");
  return html;
}

}  // namespace zebes
