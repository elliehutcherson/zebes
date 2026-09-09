#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "artwork/layered_puppet.h"
#include "artwork/profile_silhouette.h"
#include "artwork/skeleton_rig.h"

namespace zebes {

// Joint positions keyed by joint name. Sorted, because the build derived from a
// document numbers its joints in this order and that numbering must not depend
// on the order someone happened to add them in.
//
// rest_pose is bind space: where the source artwork actually sits. Every
// outline is written against it, and every frame is a departure from it.
using PuppetPose = std::map<std::string, ProfileControlPoint>;

// Which limb each joint belongs to, keyed by joint name and covering exactly
// the joint set of rest_pose. "arm_l", "spine", "tail" — the grouping a person
// means, which no bone graph recovers: a single-rooted skeleton is one graph
// and still has seven limbs. It is what a Rig Bench file records per point, so
// a skeleton authored here can be written back out as one.
using PuppetJointChains = std::map<std::string, std::string>;

// Where a skeleton came from, so the same anatomy can be re-imported when the
// rig file changes. Both fields are empty when the skeleton was authored joint
// by joint instead of imported.
struct PuppetSkeletonSource {
  std::string rig_path;
  std::string clip_id;
};

// may_stretch decides what a change in this bone's length means. Off, the bone
// only turns: pixels keep the distance from the joint they were drawn at, so a
// frame that shortens the bone leaves the artwork sticking past its end joint.
// On, the artwork is stretched along the bone to match, and a shorter bone
// really is a shorter limb.
//
// Off is the default and is what every render has always done. Turn it on for a
// limb whose length is meant to change — a tail flicking toward the viewer, a
// squash on landing — and leave it off everywhere else, because a bone whose
// length drifts by accident then silently resizes the drawing.
struct PuppetDocumentBone {
  std::string name;
  std::string start_joint;
  std::string end_joint;
  bool may_stretch = false;
};

// Hidden-surface paint under a part: the body that should still be there once
// the part moves away and stops covering it. The color is stored outright
// rather than as a point to sample, so the document means the same thing after
// the source artwork is repainted.
struct PuppetDocumentFill {
  LayeredPuppetPolygon polygon;
  std::array<uint8_t, 4> color{};
};

// One composited layer. A one-bone part is rigid; a two-bone part is skinned
// and its mesh settings apply.
//
// Which source pixels a part owns is decided in three passes: outlines claim a
// region, exclude_outlines carve holes out of that region, and exclude_parts
// subtracts whatever the named parts already claimed. A part may only exclude
// parts declared before it, because ownership is settled in document order.
//
// outlines may be empty while the part is still being authored.
struct PuppetDocumentPart {
  std::string name;
  std::vector<std::string> bones;
  std::vector<LayeredPuppetPolygon> outlines;
  std::vector<LayeredPuppetPolygon> exclude_outlines;
  std::vector<std::string> exclude_parts;
  std::vector<PuppetDocumentFill> fills;
  int mesh_spacing = 4;
  double joint_blend_radius = 12.0;
  double joint_blend_lateral_scale = 0.0;
};

// One animation frame. pose carries every joint in the document, and
// draw_order every part exactly once, back to front. Commands maintain both
// invariants, so no reader has to cope with a partial frame.
struct PuppetDocumentFrame {
  std::string name;
  PuppetPose pose;
  std::vector<std::string> draw_order;
};

// The authored state of one puppet: its source artwork, its skeleton, the
// layers cut from that artwork, and the frames those layers are posed in. This
// is what the editor and the headless CLI both read and write, and it is the
// only file a person keeps. The build input handed to the renderer is derived
// from it and never stored.
//
// Joint names are the document's keys everywhere, and rest_pose is sorted by
// name so the derived build always orders joints the same way.
//
// A document is well-formed long before it can be built. An empty skeleton, a
// part with no outline, and zero frames are all valid intermediate states;
// ValidatePuppetDocument accepts them and PuppetDocumentReadyToBuild does not.
struct PuppetDocument {
  std::string source_image;
  // A backdrop drawn under the canvas while a skeleton is being placed, so
  // joints can be traced onto a rough figure instead of onto nothing. It is a
  // drawing aid only: nothing reads its pixels and it never reaches a build.
  // Empty means no backdrop.
  std::string guide_image;
  int width = 0;
  int height = 0;
  // Whether every rendered part must come out as one connected blob. True is
  // the right default and catches a torn limb. A puppet whose art legitimately
  // separates — a hand that leaves the sleeve behind — turns it off knowingly.
  bool require_single_component = true;
  // Whether two parts may own the same source pixel.
  //
  // True lets them, which is sometimes what is wanted: a coat and the body
  // under it can share pixels on purpose. Both parts then draw that pixel and
  // carry it in different directions, so it tears in motion.
  //
  // False settles ownership by declaration order: a part gives up every pixel a
  // part declared before it already claimed. Draw a shape crossing into an
  // existing part and the new part keeps only the free side, right up to the
  // other part's edge. Nothing else changes — outlines are still whatever was
  // traced, and turning this back on restores the overlap untouched.
  bool allow_overlap = true;
  PuppetSkeletonSource skeleton_source;
  PuppetPose rest_pose;
  PuppetJointChains joint_chains;
  std::vector<PuppetDocumentBone> bones;
  // Frames per second for playback and for a clip written back out as a rig.
  // It describes the animation, not the drawing, so it belongs here rather than
  // in the browser's view state.
  int fps = 8;
  std::vector<PuppetDocumentPart> parts;
  std::vector<PuppetDocumentFrame> frames;
  // The frame the editor opens on. It changes nothing about how the puppet is
  // built; rest_pose is the pose the artwork sits in, not this.
  std::string anchor_frame;
};

// Whether a posing edit moves one frame or every frame. kAllFrames adds the
// same offset to the named joint in every frame, so each frame keeps its own
// relationship to the one being authored. This is how a correction to one
// frame's knee reaches a whole walk cycle without flattening it.
enum class PoseJointScope { kFrame, kAllFrames };

// Every edit the editor and the CLI can make. Applying a command is the only
// way a document changes, so the browser, an agent, and a test all travel the
// same path and a new capability has exactly one place to live.
namespace puppet_edit {

struct SetSourceImage {
  std::string path;
  int width = 0;
  int height = 0;
};

// Stretches the whole puppet onto a canvas of a different size: every rest
// joint, every posed joint, every outline point and every fill point is
// multiplied by the ratio between the size the document has now and the size
// given here, and the document adopts that size.
//
// This is what makes artwork drawn at another resolution usable. A build
// refuses a document whose size does not match its source PNG, so pointing a
// 256-pixel puppet at a 512-pixel drawing otherwise means retracing every
// outline. Send this in the same batch as the SetSourceImage that changes the
// size, so the two never disagree on disk.
struct ScaleToSize {
  int width = 0;
  int height = 0;
};

// Replaces the skeleton with one rig's joints and bones. The named clip's
// first frame becomes the rest pose; when import_frames is set, every frame of
// that clip is imported too, replacing any existing frames.
//
// Importing a skeleton always clears the parts' bone assignments' meaning, so
// it is refused once parts exist. Author the skeleton before cutting layers.
struct ImportSkeleton {
  SkeletonRig rig;
  std::string rig_path;
  std::string clip_id;
  bool import_frames = true;
};

// Chooses the backdrop traced against while placing joints, or clears it with
// an empty path. Unlike the source image this carries no dimensions, because
// the canvas size belongs to the artwork being cut up, not to a drawing aid.
struct SetGuideImage {
  std::string path;
};

struct AddJoint {
  std::string name;
  ProfileControlPoint rest;
  // The limb this joint belongs to. Required and never empty, because a rig
  // written out of this document records one per point and there is nothing to
  // derive it from afterwards.
  std::string chain;
};

struct RemoveJoint {
  std::string name;
};

// Moves a joint to another limb without moving the joint.
struct SetJointChain {
  std::string name;
  std::string chain;
};

// Frames per second for playback and for a clip written out as a rig.
struct SetFrameRate {
  int fps = 8;
};

// Whether two parts may own the same source pixel. Off makes a part give up
// whatever a part declared before it already claimed.
struct SetAllowOverlap {
  bool allowed = true;
};

// Whether this bone's artwork stretches when the bone's length changes.
struct SetBoneStretch {
  std::string name;
  bool may_stretch = false;
};

// Moves a joint's rest position without touching any frame.
struct MoveRestJoint {
  std::string name;
  ProfileControlPoint rest;
};

struct AddBone {
  std::string name;
  std::string start_joint;
  std::string end_joint;
};

struct RemoveBone {
  std::string name;
};

// Appends count frames named "<name_prefix>_01" upward, continuing past any
// name already taken. Each copies copy_from's pose, or the rest pose when
// copy_from is empty. Twelve frames from the rest skeleton is this command
// with count 12 and no copy_from.
struct AddFrames {
  std::string name_prefix;
  size_t count = 1;
  std::string copy_from;
};

struct RemoveFrame {
  std::string name;
};

// The whole frame order at once, rather than one move at a time, so a reorder
// either names every frame or is rejected and no frame can be lost by a
// half-applied shuffle.
struct ReorderFrames {
  std::vector<std::string> order;
};

// Frame names are what a draw order, an anchor, and an imported clip all refer
// to, so renaming one carries those references along with it.
struct RenameFrame {
  std::string name;
  std::string new_name;
};

// Aligns the selected frame's joint centroid with the rest-pose centroid,
// moving every joint of every frame by one shared amount. This does not match
// poses whose bone angles differ.
//
// One shared amount is the point. A per-joint offset would land from_frame
// exactly on the rest pose but would pull each joint independently, which
// stretches and crushes the bones between them and destroys the skeleton. A
// single slide leaves every bone the length it was.
//
// It corrects position only; RetargetFrames also calibrates angles and lengths.
struct RebaseFrames {
  std::string from_frame;
};

// Calibrates the clip's bone directions and lengths against the source drawing.
// The selected frame becomes exactly rest_pose. Other frames keep their root
// travel and each bone's angular change from that frame; rigid bones use bind
// lengths, while stretchable bones retain their relative length changes.
// Requires a forest with nonzero bones in the source and every frame.
struct RetargetFrames {
  std::string from_frame;
};

// Rewrites one frame so every bone is the length it is in the rest pose, while
// keeping the direction the frame points it in.
//
// This changes projected geometry and may remove intentional foreshortening.
// Rigid parts rotate without scaling; only may_stretch bones scale artwork
// when a frame's bone length differs from its bind length.
//
// Joints are moved outward from the root, so a corrected upper arm carries the
// forearm and paw with it. A joint that is the end of two bones has no single
// parent and stays where the frame put it.
struct FitBoneLengths {
  std::string frame;
};

struct SetAnchorFrame {
  std::string name;
};

struct PoseJoint {
  std::string frame;
  std::string joint;
  ProfileControlPoint point;
  PoseJointScope scope = PoseJointScope::kFrame;
};

struct AddPart {
  std::string name;
  std::vector<std::string> bones;
};

struct RemovePart {
  std::string name;
};

// Part names are what a draw order and another part's exclude list refer to, so
// renaming one carries those references along with it. The outline, the fills
// and the mesh settings stay put; only the name changes.
//
// A name is a label. It does not decide which pixels move with which bone, so
// renaming a part that was built on the wrong bone leaves it bent by the wrong
// bone under a better name. SetPartBones is the command that fixes that.
struct RenamePart {
  std::string name;
  std::string new_name;
};

// Points a part at different bones, keeping its outline, fills and mesh
// settings. Naming the wrong bone is easy — an anatomical left arm is drawn on
// screen-right — and without this the only repair is to delete the part and
// trace it again.
struct SetPartBones {
  std::string part;
  std::vector<std::string> bones;
};

struct SetPartOutline {
  std::string part;
  std::vector<LayeredPuppetPolygon> outlines;
};

// Holes carved out of the part's own outlines, for a region the outline is
// easier to draw around than to avoid.
struct SetPartExcludeOutlines {
  std::string part;
  std::vector<LayeredPuppetPolygon> outlines;
};

// Parts whose claimed pixels this one gives up, so a body and the limb in front
// of it never both paint the same pixel. Named parts must be declared earlier.
struct SetPartExcludeParts {
  std::string part;
  std::vector<std::string> excluded;
};

struct SetPartFills {
  std::string part;
  std::vector<PuppetDocumentFill> fills;
};

struct SetPartMesh {
  std::string part;
  int spacing = 4;
  double joint_blend_radius = 12.0;
  double joint_blend_lateral_scale = 0.0;
};

struct SetDrawOrder {
  std::string frame;
  std::vector<std::string> order;
};

using Command =
    std::variant<SetSourceImage, ScaleToSize, SetGuideImage, ImportSkeleton, AddJoint, RemoveJoint,
                 SetJointChain, SetFrameRate, SetAllowOverlap, SetBoneStretch, MoveRestJoint,
                 AddBone, RemoveBone, AddFrames, RemoveFrame, ReorderFrames, RenameFrame,
                 RebaseFrames, RetargetFrames, FitBoneLengths, SetAnchorFrame, PoseJoint, AddPart,
                 RemovePart, RenamePart, SetPartBones, SetPartOutline, SetPartExcludeOutlines,
                 SetPartExcludeParts, SetPartFills, SetPartMesh, SetDrawOrder>;

}  // namespace puppet_edit

// Applies one command. The document is left unchanged when the command is
// rejected, so a failed edit never leaves a half-applied skeleton behind.
absl::Status ApplyPuppetCommand(PuppetDocument& document, const puppet_edit::Command& command);

// Invariants that hold after every accepted command: unique names, bones and
// parts that reference joints and bones the document owns, at most two bones
// per part, a pose covering exactly the joint set in every frame, a draw order
// naming every part exactly once, and an anchor that is empty or an existing
// frame. Commands maintain these, so a failure here is a bug rather than user
// error; the CLI checks it after every batch to catch one early.
absl::Status ValidatePuppetDocument(const PuppetDocument& document);

// Whether the document has everything the renderer requires: a source image,
// at least one joint, one bone, one part, and one frame, every part carrying an
// outline and a legal bone chain. The message names the first thing missing, in
// the order the editor asks for it, so it can be shown to the author unchanged.
absl::Status PuppetDocumentReadyToBuild(const PuppetDocument& document);

}  // namespace zebes
