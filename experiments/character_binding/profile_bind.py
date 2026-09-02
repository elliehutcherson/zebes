"""Prototype semantics over C++-extracted profile topology.

The C++ silhouette and medial axis are authoritative. This disposable Python
layer explores joint inference, bone ownership, and posed deformation without
duplicating stable isolation, reduction, thinning, or branch pruning.
"""

from __future__ import annotations

import json
import math
from collections import deque
from dataclasses import asdict, dataclass
from pathlib import Path

from .png import write_rgba

POSES = ("neutral", "contact", "passing", "airborne")
POSE_FRONT_SUFFIX = {
    "neutral": "b",
    "contact": "a",
    "passing": "b",
    "airborne": "a",
}
BONE_COLORS = (
    (44, 123, 229, 255),
    (230, 126, 34, 255),
    (46, 204, 113, 255),
    (155, 89, 182, 255),
    (241, 196, 15, 255),
    (231, 76, 60, 255),
    (26, 188, 156, 255),
    (149, 165, 166, 255),
)

Point = tuple[float, float]


class BindingError(ValueError):
    """Raised when a silhouette cannot yield an unambiguous profile binding."""


@dataclass(frozen=True)
class Bone:
    name: str
    start: str
    end: str


@dataclass(frozen=True)
class ProfileBinding:
    width: int
    height: int
    source_scale: int
    mask: bytes
    skeleton: bytes
    joints: dict[str, Point]
    bones: tuple[Bone, ...]
    labels: tuple[int, ...]

    def report(self) -> str:
        lines = [
            f"profile binding at {self.width}x{self.height} "
            f"(source scale 1:{self.source_scale})",
            f"  silhouette pixels {sum(self.mask)}",
            f"  medial-axis pixels {sum(self.skeleton)}",
            f"  semantic joints {len(self.joints)}",
            f"  bound bones {len(self.bones)}",
        ]
        for name, (x, y) in sorted(self.joints.items()):
            lines.append(f"  {name:<12} ({x:6.1f}, {y:6.1f})")
        return "\n".join(lines)




def _neighbors(mask: bytes | bytearray, width: int, height: int, index: int) -> list[int]:
    x, y = index % width, index // width
    result: list[int] = []
    for offset_y in (-1, 0, 1):
        for offset_x in (-1, 0, 1):
            if offset_x == 0 and offset_y == 0:
                continue
            neighbor_x, neighbor_y = x + offset_x, y + offset_y
            if not 0 <= neighbor_x < width or not 0 <= neighbor_y < height:
                continue
            neighbor = neighbor_y * width + neighbor_x
            if mask[neighbor]:
                result.append(neighbor)
    return result




def _components(mask: bytes | bytearray, width: int, height: int) -> list[list[int]]:
    seen = bytearray(len(mask))
    components: list[list[int]] = []
    for start, covered in enumerate(mask):
        if not covered or seen[start]:
            continue
        component: list[int] = []
        pending = [start]
        seen[start] = 1
        while pending:
            current = pending.pop()
            component.append(current)
            for neighbor in _neighbors(mask, width, height, current):
                if seen[neighbor]:
                    continue
                seen[neighbor] = 1
                pending.append(neighbor)
        components.append(component)
    components.sort(key=len, reverse=True)
    return components


def _shortest_path(
    skeleton: bytes | bytearray, width: int, height: int, start: int, end: int
) -> list[int]:
    pending = deque([start])
    previous = {start: -1}
    while pending:
        current = pending.popleft()
        if current == end:
            break
        for neighbor in _neighbors(skeleton, width, height, current):
            if neighbor in previous:
                continue
            previous[neighbor] = current
            pending.append(neighbor)
    if end not in previous:
        raise BindingError("semantic skeleton points are disconnected")
    path = []
    current = end
    while current >= 0:
        path.append(current)
        current = previous[current]
    path.reverse()
    return path


def _nearest_skeleton_point(
    skeleton: bytes | bytearray, width: int, point: Point, component: set[int] | None = None
) -> int:
    candidates = component if component is not None else {
        index for index, covered in enumerate(skeleton) if covered
    }
    if not candidates:
        raise BindingError("silhouette medial axis is empty")
    return min(
        candidates,
        key=lambda index: (index % width - point[0]) ** 2 + (index // width - point[1]) ** 2,
    )


def _endpoints(
    skeleton: bytes | bytearray, width: int, height: int
) -> list[int]:
    return [
        index
        for index, covered in enumerate(skeleton)
        if covered and len(_neighbors(skeleton, width, height, index)) == 1
    ]


def _distance_to_background(mask: bytes | bytearray, width: int, height: int) -> list[int]:
    distance = [-1] * len(mask)
    pending: deque[int] = deque()
    for index, covered in enumerate(mask):
        if not covered:
            distance[index] = 0
            pending.append(index)
    while pending:
        current = pending.popleft()
        x, y = current % width, current // width
        for offset_x, offset_y in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nx, ny = x + offset_x, y + offset_y
            if not 0 <= nx < width or not 0 <= ny < height:
                continue
            neighbor = ny * width + nx
            if distance[neighbor] >= 0:
                continue
            distance[neighbor] = distance[current] + 1
            pending.append(neighbor)
    return distance


def _point(index: int, width: int) -> Point:
    return float(index % width), float(index // width)


def _path_point_at_fraction(path: list[int], width: int, fraction: float) -> Point:
    index = path[min(len(path) - 1, max(0, round((len(path) - 1) * fraction)))]
    return _point(index, width)


def infer_joints(
    mask: bytes | bytearray, skeleton: bytes | bytearray, width: int, height: int
) -> dict[str, Point]:
    """Infer a side-view humanoid joint set from silhouette topology."""
    occupied = [index for index, covered in enumerate(mask) if covered]
    if not occupied:
        raise BindingError("profile silhouette is empty")
    top = min(index // width for index in occupied)
    bottom = max(index // width for index in occupied)
    left = min(index % width for index in occupied)
    right = max(index % width for index in occupied)
    figure_height = bottom - top + 1

    components = _components(skeleton, width, height)
    if not components:
        raise BindingError("profile silhouette produced no medial axis")
    main = set(components[0])
    endpoints = _endpoints(skeleton, width, height)
    feet = sorted(
        (index for index in endpoints if index // width >= top + figure_height * 0.72),
        key=lambda index: index % width,
    )
    if len(feet) < 2:
        # A boot with an enclosed opening can produce a closed medial-axis loop
        # and therefore no endpoint. Fall back to the lowest main-component
        # extremity on each side; the C++ topology fuzz set found this case.
        center_x = (left + right) / 2.0
        lower = [
            index
            for index in main
            if index // width >= top + figure_height * 0.72
        ]
        feet = []
        for side in (-1, 1):
            candidates = [
                index
                for index in lower
                if (index % width - center_x) * side > 0
            ]
            if candidates:
                feet.append(
                    max(
                        candidates,
                        key=lambda index: (
                            index // width,
                            abs(index % width - center_x),
                        ),
                    )
                )
        feet.sort(key=lambda index: index % width)
    if len(feet) < 2:
        raise BindingError("profile silhouette does not expose two separate feet")
    foot_a, foot_b = feet[0], feet[-1]
    if foot_a not in main or foot_b not in main:
        raise BindingError("both feet must connect to the main body silhouette")

    leg_path = _shortest_path(skeleton, width, height, foot_a, foot_b)
    split_index = min(leg_path, key=lambda index: index // width)
    split = _point(split_index, width)

    distances = _distance_to_background(mask, width, height)
    head_candidates = [
        index
        for index in main
        if index // width < top + figure_height * 0.48
        and left + (right - left) * 0.18
        < index % width
        < right - (right - left) * 0.18
    ]
    if not head_candidates:
        raise BindingError("could not locate the head interior")
    head_index = max(head_candidates, key=lambda index: distances[index])
    trunk_path = _shortest_path(skeleton, width, height, head_index, split_index)

    neck_target_y = top + figure_height * 0.37
    shoulder_target_y = top + figure_height * 0.48
    neck_index = min(trunk_path, key=lambda index: abs(index // width - neck_target_y))
    shoulder_index = min(
        trunk_path, key=lambda index: abs(index // width - shoulder_target_y)
    )
    neck = _point(neck_index, width)
    shoulder = _point(shoulder_index, width)

    # The visible split is a coat hem on this reference, not the pelvis. Put the
    # anatomical hip on the medial trunk and retain the hem as a binding boundary.
    hip_target_y = shoulder[1] + (split[1] - shoulder[1]) * 0.58
    hip_index = min(trunk_path, key=lambda index: abs(index // width - hip_target_y))
    hip = _point(hip_index, width)
    path_a = _shortest_path(skeleton, width, height, hip_index, foot_a)
    path_b = _shortest_path(skeleton, width, height, hip_index, foot_b)

    middle_points = [
        index
        for index, covered in enumerate(skeleton)
        if covered
        and neck[1] + 6 <= index // width <= hip[1] + 12
        and abs(index % width - shoulder[0]) >= figure_height * 0.055
    ]
    hands: list[int] = []
    for side in (-1, 1):
        candidates = [
            index
            for index in middle_points
            if (index % width - shoulder[0]) * side > 0
        ]
        if candidates:
            hands.append(
                max(
                    candidates,
                    key=lambda index: (index % width - shoulder[0]) ** 2
                    + (index // width - shoulder[1]) ** 2,
                )
            )

    joints: dict[str, Point] = {
        "head": _point(head_index, width),
        "neck": neck,
        "shoulder": shoulder,
        "hip": hip,
        "leg_split": split,
        "knee_a": (
            hip[0] + (_point(foot_a, width)[0] - hip[0]) * 0.58,
            hip[1] + (_point(foot_a, width)[1] - hip[1]) * 0.58,
        ),
        "foot_a": _point(foot_a, width),
        "knee_b": (
            hip[0] + (_point(foot_b, width)[0] - hip[0]) * 0.58,
            hip[1] + (_point(foot_b, width)[1] - hip[1]) * 0.58,
        ),
        "foot_b": _point(foot_b, width),
    }
    for index, hand_index in enumerate(hands[:2]):
        hand = _point(hand_index, width)
        if hand_index in main:
            arm_path = _shortest_path(
                skeleton, width, height, shoulder_index, hand_index
            )
            direct_length = math.dist(shoulder, hand)
            if len(arm_path) <= direct_length * 1.8:
                elbow = _path_point_at_fraction(arm_path, width, 0.55)
            else:
                elbow = (
                    (shoulder[0] + hand[0]) / 2.0,
                    (shoulder[1] + hand[1]) / 2.0,
                )
        else:
            elbow = (
                (shoulder[0] + hand[0]) / 2.0,
                (shoulder[1] + hand[1]) / 2.0,
            )
        suffix = "a" if index == 0 else "b"
        joints[f"elbow_{suffix}"] = elbow
        joints[f"hand_{suffix}"] = hand
    return joints


def bones_for(joints: dict[str, Point]) -> tuple[Bone, ...]:
    bones = [
        Bone("head", "neck", "head"),
        Bone("torso", "shoulder", "hip"),
        Bone("thigh_a", "hip", "knee_a"),
        Bone("shin_a", "knee_a", "foot_a"),
        Bone("thigh_b", "hip", "knee_b"),
        Bone("shin_b", "knee_b", "foot_b"),
    ]
    for suffix in ("a", "b"):
        if f"hand_{suffix}" not in joints:
            continue
        bones.extend(
            (
                Bone(f"upper_arm_{suffix}", "shoulder", f"elbow_{suffix}"),
                Bone(f"forearm_{suffix}", f"elbow_{suffix}", f"hand_{suffix}"),
            )
        )
    return tuple(bones)


def _distance_to_segment(point: Point, start: Point, end: Point) -> float:
    vx, vy = end[0] - start[0], end[1] - start[1]
    length_squared = vx * vx + vy * vy
    if length_squared <= 1e-9:
        return math.dist(point, start)
    t = max(0.0, min(1.0, ((point[0] - start[0]) * vx + (point[1] - start[1]) * vy) / length_squared))
    projection = (start[0] + t * vx, start[1] + t * vy)
    return math.dist(point, projection)


def bind_pixels(
    mask: bytes | bytearray,
    width: int,
    joints: dict[str, Point],
    bones: tuple[Bone, ...],
) -> tuple[int, ...]:
    """Assign every silhouette pixel to one persistent semantic bone."""
    neck_y = joints["neck"][1]
    leg_split_y = joints["leg_split"][1]
    torso_index = next(i for i, bone in enumerate(bones) if bone.name == "torso")
    arm_candidates = [i for i, bone in enumerate(bones) if "arm" in bone.name]
    labels = [-1] * len(mask)
    for index, covered in enumerate(mask):
        if not covered:
            continue
        point = _point(index, width)
        if point[1] <= neck_y:
            candidates = [0]
        elif point[1] >= leg_split_y:
            candidates = [
                i
                for i, bone in enumerate(bones)
                if bone.name.startswith(("thigh", "shin"))
            ]
        else:
            nearest_arm = min(
                arm_candidates,
                key=lambda bone_index: _distance_to_segment(
                    point,
                    joints[bones[bone_index].start],
                    joints[bones[bone_index].end],
                ),
            )
            arm_distance = _distance_to_segment(
                point,
                joints[bones[nearest_arm].start],
                joints[bones[nearest_arm].end],
            )
            torso_center_x = (
                joints["shoulder"][0] + joints["hip"][0]
            ) / 2.0
            outside_core = abs(point[0] - torso_center_x) >= width * 0.055
            candidates = (
                [nearest_arm]
                if outside_core and arm_distance <= width * 0.052
                else [torso_index]
            )
        labels[index] = min(
            candidates,
            key=lambda bone_index: _distance_to_segment(
                point,
                joints[bones[bone_index].start],
                joints[bones[bone_index].end],
            ),
        )
    return tuple(labels)


def make_binding_from_topology(
    mask: bytes | bytearray,
    skeleton: bytes | bytearray,
    width: int,
    height: int,
    source_scale: int,
) -> ProfileBinding:
    """Bind semantics to topology produced by the C++ profile extractor."""
    if width <= 0 or height <= 0 or source_scale <= 0:
        raise BindingError("profile topology dimensions and source scale must be positive")
    if len(mask) != width * height or len(skeleton) != width * height:
        raise BindingError("profile topology buffers do not match their dimensions")
    if not any(mask) or not any(skeleton):
        raise BindingError("profile topology must contain silhouette and medial-axis pixels")

    joints = infer_joints(mask, skeleton, width, height)
    bones = bones_for(joints)
    labels = bind_pixels(mask, width, joints, bones)
    return ProfileBinding(
        width=width,
        height=height,
        source_scale=source_scale,
        mask=bytes(mask),
        skeleton=bytes(skeleton),
        joints=joints,
        bones=bones,
        labels=labels,
    )


def _chain_point(parent: Point, length: float, angle_degrees: float) -> Point:
    angle = math.radians(angle_degrees)
    return parent[0] + math.sin(angle) * length, parent[1] + math.cos(angle) * length


def pose_joints(binding: ProfileBinding, pose: str) -> dict[str, Point]:
    if pose not in POSES:
        raise BindingError(f"unknown profile pose {pose!r}; available: {POSES}")
    source = binding.joints
    if pose == "neutral":
        return dict(source)

    target = dict(source)
    hip = source["hip"]
    shoulder = source["shoulder"]
    leg_angles = {
        "contact": ((-34.0, -12.0), (31.0, 58.0)),
        "passing": ((-8.0, 24.0), (12.0, -38.0)),
        "airborne": ((-28.0, 58.0), (26.0, -52.0)),
    }[pose]
    arm_angles = {
        "contact": ((32.0, 58.0), (-35.0, -62.0)),
        "passing": ((12.0, 34.0), (-16.0, -38.0)),
        "airborne": ((48.0, 76.0), (-52.0, -82.0)),
    }[pose]

    for index, suffix in enumerate(("a", "b")):
        upper_length = math.dist(source["hip"], source[f"knee_{suffix}"])
        lower_length = math.dist(source[f"knee_{suffix}"], source[f"foot_{suffix}"])
        knee = _chain_point(hip, upper_length, leg_angles[index][0])
        target[f"knee_{suffix}"] = knee
        target[f"foot_{suffix}"] = _chain_point(knee, lower_length, leg_angles[index][1])
        if f"hand_{suffix}" not in source:
            continue
        upper_arm = math.dist(source["shoulder"], source[f"elbow_{suffix}"])
        forearm = math.dist(source[f"elbow_{suffix}"], source[f"hand_{suffix}"])
        elbow = _chain_point(shoulder, upper_arm, arm_angles[index][0])
        target[f"elbow_{suffix}"] = elbow
        target[f"hand_{suffix}"] = _chain_point(elbow, forearm, arm_angles[index][1])

    if pose == "airborne":
        for name, (x, y) in tuple(target.items()):
            target[name] = (x, y - 12.0)
    return target


def _transform_point(point: Point, source_start: Point, source_end: Point, target_start: Point, target_end: Point) -> Point:
    source_dx, source_dy = source_end[0] - source_start[0], source_end[1] - source_start[1]
    source_length = math.hypot(source_dx, source_dy)
    target_dx, target_dy = target_end[0] - target_start[0], target_end[1] - target_start[1]
    target_length = math.hypot(target_dx, target_dy)
    if source_length <= 1e-6 or target_length <= 1e-6:
        return point
    source_unit = (source_dx / source_length, source_dy / source_length)
    source_perpendicular = (-source_unit[1], source_unit[0])
    relative = (point[0] - source_start[0], point[1] - source_start[1])
    along = relative[0] * source_unit[0] + relative[1] * source_unit[1]
    away = relative[0] * source_perpendicular[0] + relative[1] * source_perpendicular[1]
    target_unit = (target_dx / target_length, target_dy / target_length)
    target_perpendicular = (-target_unit[1], target_unit[0])
    return (
        target_start[0] + target_unit[0] * along + target_perpendicular[0] * away,
        target_start[1] + target_unit[1] * along + target_perpendicular[1] * away,
    )


def render_pose(binding: ProfileBinding, pose: str) -> tuple[bytearray, dict[str, Point]]:
    target_joints = pose_joints(binding, pose)
    output = bytearray(binding.width * binding.height)
    for index, label in enumerate(binding.labels):
        if label < 0:
            continue
        bone = binding.bones[label]
        target = _transform_point(
            _point(index, binding.width),
            binding.joints[bone.start],
            binding.joints[bone.end],
            target_joints[bone.start],
            target_joints[bone.end],
        )
        target_x, target_y = round(target[0]), round(target[1])
        for offset_y in (0, 1):
            for offset_x in (0, 1):
                x, y = target_x + offset_x, target_y + offset_y
                if 0 <= x < binding.width and 0 <= y < binding.height:
                    output[y * binding.width + x] = 1
    return _fill_holes(output, binding.width, binding.height), target_joints


def depths_for_pose(binding: ProfileBinding, pose: str) -> tuple[int, ...]:
    """Ordinal grayscale policy; which limb is front remains experiment-owned."""
    if pose not in POSE_FRONT_SUFFIX:
        raise BindingError(f"unknown profile pose {pose!r}; available: {POSES}")
    front_suffix = POSE_FRONT_SUFFIX[pose]
    depths = []
    for bone in binding.bones:
        if bone.name == "head":
            depths.append(200)
        elif bone.name == "torso":
            depths.append(140)
        elif bone.name.endswith(f"_{front_suffix}"):
            depths.append(220)
        else:
            depths.append(70)
    return tuple(depths)


def render_pose_layers(
    binding: ProfileBinding, pose: str
) -> tuple[bytearray, dict[str, Point]]:
    """Pose 1-based bone IDs, resolving overlaps by declared ordinal depth."""
    posed_mask, target_joints = render_pose(binding, pose)
    depths = depths_for_pose(binding, pose)
    output = bytearray(binding.width * binding.height)
    output_depth = bytearray(binding.width * binding.height)
    for index, label in enumerate(binding.labels):
        if label < 0:
            continue
        bone = binding.bones[label]
        target = _transform_point(
            _point(index, binding.width),
            binding.joints[bone.start],
            binding.joints[bone.end],
            target_joints[bone.start],
            target_joints[bone.end],
        )
        target_x, target_y = round(target[0]), round(target[1])
        for offset_y in (0, 1):
            for offset_x in (0, 1):
                x, y = target_x + offset_x, target_y + offset_y
                if not 0 <= x < binding.width or not 0 <= y < binding.height:
                    continue
                target_index = y * binding.width + x
                if not posed_mask[target_index]:
                    continue
                if output[target_index] and output_depth[target_index] > depths[label]:
                    continue
                output[target_index] = label + 1
                output_depth[target_index] = depths[label]

    pending: deque[int] = deque(
        index for index, layer in enumerate(output) if layer
    )
    reached = bytearray(1 if layer else 0 for layer in output)
    while pending:
        index = pending.popleft()
        x, y = index % binding.width, index // binding.width
        for offset_x, offset_y in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nx, ny = x + offset_x, y + offset_y
            if not 0 <= nx < binding.width or not 0 <= ny < binding.height:
                continue
            neighbor = ny * binding.width + nx
            if not posed_mask[neighbor] or reached[neighbor]:
                continue
            reached[neighbor] = 1
            output[neighbor] = output[index]
            output_depth[neighbor] = output_depth[index]
            pending.append(neighbor)
    return output, target_joints


def _draw_line(
    pixels: bytearray,
    width: int,
    height: int,
    start: Point,
    end: Point,
    color: tuple[int, int, int, int],
) -> None:
    x0, y0 = round(start[0]), round(start[1])
    x1, y1 = round(end[0]), round(end[1])
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    error = dx + dy
    while True:
        if 0 <= x0 < width and 0 <= y0 < height:
            offset = (y0 * width + x0) * 4
            pixels[offset : offset + 4] = bytes(color)
        if x0 == x1 and y0 == y1:
            break
        doubled = 2 * error
        if doubled >= dy:
            error += dy
            x0 += sx
        if doubled <= dx:
            error += dx
            y0 += sy


def _fill_holes(mask: bytearray, width: int, height: int) -> bytearray:
    exterior = bytearray(len(mask))
    pending: deque[int] = deque()

    def enqueue(index: int) -> None:
        if mask[index] or exterior[index]:
            return
        exterior[index] = 1
        pending.append(index)

    for x in range(width):
        enqueue(x)
        enqueue((height - 1) * width + x)
    for y in range(height):
        enqueue(y * width)
        enqueue(y * width + width - 1)
    while pending:
        index = pending.popleft()
        x, y = index % width, index // width
        if x > 0:
            enqueue(index - 1)
        if x + 1 < width:
            enqueue(index + 1)
        if y > 0:
            enqueue(index - width)
        if y + 1 < height:
            enqueue(index + width)
    return bytearray(
        1 if covered or not exterior[index] else 0
        for index, covered in enumerate(mask)
    )


def _mask_rgba(mask: bytes | bytearray, width: int, height: int) -> bytearray:
    pixels = bytearray(b"\x00\x00\x00\xff" * width * height)
    for index, covered in enumerate(mask):
        if covered:
            pixels[index * 4 : index * 4 + 4] = b"\xff\xff\xff\xff"
    return pixels


def downsample_subject_rgba(
    pixels: bytes | bytearray,
    mask: bytes | bytearray,
    width: int,
    height: int,
    binding: ProfileBinding,
) -> bytearray:
    """Average only isolated subject pixels into the binding resolution."""
    if len(pixels) != width * height * 4 or len(mask) != width * height:
        raise BindingError("source color image and isolation mask dimensions differ")
    if width != binding.width * binding.source_scale or height != binding.height * binding.source_scale:
        raise BindingError("source color image does not match the profile binding scale")

    output = bytearray(binding.width * binding.height * 4)
    scale = binding.source_scale
    for output_y in range(binding.height):
        for output_x in range(binding.width):
            output_index = output_y * binding.width + output_x
            if not binding.mask[output_index]:
                continue
            totals = [0, 0, 0]
            samples = 0
            for source_y in range(output_y * scale, (output_y + 1) * scale):
                for source_x in range(output_x * scale, (output_x + 1) * scale):
                    source_index = source_y * width + source_x
                    if not mask[source_index]:
                        continue
                    offset = source_index * 4
                    for channel in range(3):
                        totals[channel] += pixels[offset + channel]
                    samples += 1
            if samples == 0:
                continue
            offset = output_index * 4
            output[offset : offset + 4] = bytes(
                (
                    totals[0] // samples,
                    totals[1] // samples,
                    totals[2] // samples,
                    255,
                )
            )
    return output


def render_color_pose(
    binding: ProfileBinding, source_pixels: bytes | bytearray, pose: str
) -> bytearray:
    """Warp isolated source colors with the same bones as the posed mask."""
    if len(source_pixels) != binding.width * binding.height * 4:
        raise BindingError("downsampled source colors do not match the binding")
    posed_mask, target_joints = render_pose(binding, pose)
    output = bytearray(binding.width * binding.height * 4)
    for index, label in enumerate(binding.labels):
        if label < 0:
            continue
        source_offset = index * 4
        if source_pixels[source_offset + 3] == 0:
            continue
        bone = binding.bones[label]
        target = _transform_point(
            _point(index, binding.width),
            binding.joints[bone.start],
            binding.joints[bone.end],
            target_joints[bone.start],
            target_joints[bone.end],
        )
        target_x, target_y = round(target[0]), round(target[1])
        for offset_y in (0, 1):
            for offset_x in (0, 1):
                x, y = target_x + offset_x, target_y + offset_y
                if not 0 <= x < binding.width or not 0 <= y < binding.height:
                    continue
                target_index = y * binding.width + x
                if not posed_mask[target_index]:
                    continue
                target_offset = target_index * 4
                output[target_offset : target_offset + 4] = source_pixels[
                    source_offset : source_offset + 4
                ]

    # Rigid pieces expose small gaps at bends and reveal surfaces hidden in the
    # neutral reference. Fill only inside the already-approved posed silhouette,
    # using the nearest warped source color. This cannot invent detail outside
    # the structural guide.
    pending: deque[int] = deque()
    reached = bytearray(len(posed_mask))
    for index, covered in enumerate(posed_mask):
        if covered and output[index * 4 + 3]:
            reached[index] = 1
            pending.append(index)
    while pending:
        index = pending.popleft()
        x, y = index % binding.width, index // binding.width
        for offset_x, offset_y in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nx, ny = x + offset_x, y + offset_y
            if not 0 <= nx < binding.width or not 0 <= ny < binding.height:
                continue
            neighbor = ny * binding.width + nx
            if not posed_mask[neighbor] or reached[neighbor]:
                continue
            reached[neighbor] = 1
            source_offset = index * 4
            target_offset = neighbor * 4
            output[target_offset : target_offset + 4] = output[
                source_offset : source_offset + 4
            ]
            pending.append(neighbor)
    return output




def write_evidence(
    out: Path,
    binding: ProfileBinding,
    source_pixels: bytes | bytearray | None = None,
    source_mask: bytes | bytearray | None = None,
    source_width: int | None = None,
    source_height: int | None = None,
) -> None:
    out.mkdir(parents=True, exist_ok=True)
    skeleton_pixels = bytearray(b"\x00\x00\x00\xff" * binding.width * binding.height)
    for index, covered in enumerate(binding.mask):
        if covered:
            skeleton_pixels[index * 4 : index * 4 + 4] = b"\xd0\xd0\xd0\xff"
    for index, covered in enumerate(binding.skeleton):
        if covered:
            skeleton_pixels[index * 4 : index * 4 + 4] = b"\xff\x46\x46\xff"
    for bone in binding.bones:
        _draw_line(
            skeleton_pixels,
            binding.width,
            binding.height,
            binding.joints[bone.start],
            binding.joints[bone.end],
            (255, 220, 0, 255),
        )
    for x, y in binding.joints.values():
        for offset_y in range(-2, 3):
            for offset_x in range(-2, 3):
                px, py = round(x) + offset_x, round(y) + offset_y
                if 0 <= px < binding.width and 0 <= py < binding.height:
                    offset = (py * binding.width + px) * 4
                    skeleton_pixels[offset : offset + 4] = b"\x00\x78\xff\xff"
    write_rgba(out / "skeleton.png", binding.width, binding.height, skeleton_pixels)

    regions = bytearray(b"\x00\x00\x00\xff" * binding.width * binding.height)
    for index, label in enumerate(binding.labels):
        if label < 0:
            continue
        regions[index * 4 : index * 4 + 4] = bytes(BONE_COLORS[label % len(BONE_COLORS)])
    write_rgba(out / "binding-regions.png", binding.width, binding.height, regions)
    reduced_source = None
    if source_pixels is not None:
        if source_mask is None or source_width is None or source_height is None:
            raise BindingError("color evidence requires source mask and dimensions")
        reduced_source = downsample_subject_rgba(
            source_pixels,
            source_mask,
            source_width,
            source_height,
            binding,
        )
        write_rgba(
            out / "source-color.png",
            binding.width,
            binding.height,
            reduced_source,
        )


    pose_manifest = {}
    depth_manifest = {}
    for pose in POSES:
        layers, joints = render_pose_layers(binding, pose)
        posed = bytearray(1 if layer else 0 for layer in layers)
        pose_pixels = _mask_rgba(posed, binding.width, binding.height)
        write_rgba(
            out / f"pose-{pose}.png",
            binding.width,
            binding.height,
            pose_pixels,
        )
        layer_pixels = bytearray(b"\x00\x00\x00\xff" * binding.width * binding.height)
        for index, layer in enumerate(layers):
            layer_pixels[index * 4] = layer
        write_rgba(
            out / f"pose-{pose}-layers.png",
            binding.width,
            binding.height,
            layer_pixels,
        )
        if reduced_source is not None:
            write_rgba(
                out / f"pose-{pose}-color.png",
                binding.width,
                binding.height,
                render_color_pose(binding, reduced_source, pose),
            )
        for bone in binding.bones:
            _draw_line(
                pose_pixels,
                binding.width,
                binding.height,
                joints[bone.start],
                joints[bone.end],
                (255, 210, 0, 255),
            )
        for x, y in joints.values():
            for offset_y in range(-2, 3):
                for offset_x in range(-2, 3):
                    px, py = round(x) + offset_x, round(y) + offset_y
                    if 0 <= px < binding.width and 0 <= py < binding.height:
                        offset = (py * binding.width + px) * 4
                        pose_pixels[offset : offset + 4] = b"\x00\x78\xff\xff"
        write_rgba(
            out / f"pose-{pose}-wireframe.png",
            binding.width,
            binding.height,
            pose_pixels,
        )
        pose_manifest[pose] = {
            name: [round(x, 3), round(y, 3)]
            for name, (x, y) in joints.items()
        }
        depth_manifest[pose] = {
            bone.name: depth
            for bone, depth in zip(
                binding.bones, depths_for_pose(binding, pose), strict=True
            )
        }

    manifest = {
        "version": 1,
        "width": binding.width,
        "height": binding.height,
        "source_scale": binding.source_scale,
        "joints": {name: [round(x, 3), round(y, 3)] for name, (x, y) in binding.joints.items()},
        "bones": [asdict(bone) for bone in binding.bones],
        "poses": pose_manifest,
        "depths": depth_manifest,
    }
    (out / "binding.json").write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
