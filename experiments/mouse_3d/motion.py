"""Authored sagittal run, in meters, independent of Blender and the engine."""

import math

THIGH = 0.84
SHIN = 0.78
STANCE = 0.4
FRAMES = 24
FPS = 24
HIP_HALF_WIDTH = 0.375
SHOULDER_HALF_WIDTH = 0.635


def two_bone_joint(start, end, first, second, bend=1):
    """Solve a fixed-length chain; unreachable targets are authoring errors."""
    values = (*start, *end, first, second)
    if not all(math.isfinite(value) for value in values) or min(first, second) <= 0:
        raise ValueError("joint coordinates and positive lengths must be finite")
    dx, dz = end[0] - start[0], end[1] - start[1]
    distance = math.hypot(dx, dz)
    if not abs(first - second) < distance < first + second:
        raise ValueError(f"unreachable two-bone target: distance={distance}")
    if bend not in (-1, 1):
        raise ValueError("bend must select one of the two solutions")
    along = (first * first - second * second + distance * distance) / (2 * distance)
    across = math.sqrt(first * first - along * along) * bend
    return (start[0] + along * dx / distance - across * dz / distance,
            start[1] + along * dz / distance + across * dx / distance)


def foot_path(phase):
    phase %= 1.0
    if phase < STANCE:
        return (0.68 - 1.36 * phase / STANCE, 0.25, 0.0, True, 0.0)
    recovery = (phase - STANCE) / (1 - STANCE)
    # Hold the heel behind the body early in recovery, then reach forwards.
    x = -0.68 + 1.36 * (3 * recovery ** 2 - 2 * recovery ** 3)
    angle = -0.95 * math.sin(math.pi * recovery)
    clearance = 0.55 * math.sin(math.pi * recovery) ** 1.4
    # These are the conservative bounds of the rigid foot below its ankle.
    bottom = min(xp * math.sin(angle) + zp * math.cos(angle)
                 for xp in (-0.23, 0.63) for zp in (-0.25, 0.15))
    return x, clearance - bottom, angle, False, clearance


def pose_at(time):
    if not math.isfinite(time):
        raise ValueError("animation time must be finite")
    time %= 1.0
    hip_z = 1.57 - 0.075 * math.sin(4 * math.pi * time)
    bob = hip_z - 1.57
    bones = {
        "root": ((0, 0, hip_z), (0, 0, hip_z + 0.24)),
        "torso": ((0, 0, hip_z), (0.18, 0, 2.64 + bob)),
        "head": ((0.18, 0, 2.64 + bob), (0.28, 0, 3.40 + bob)),
    }
    feet = {}
    for side, offset, y in (("near", 0, -HIP_HALF_WIDTH), ("far", 0.5, HIP_HALF_WIDTH)):
        phase = (time + offset) % 1.0
        x, z, angle, planted, clearance = foot_path(phase)
        hip, ankle = (0, hip_z), (x, z)
        knee = two_bone_joint(hip, ankle, THIGH, SHIN)
        h, k, a = (hip[0], y, hip[1]), (knee[0], y, knee[1]), (x, y, z)
        toe = (x + 0.5 * math.cos(angle), y, z + 0.5 * math.sin(angle))
        bones[f"thigh.{side}"] = (h, k)
        bones[f"shin.{side}"] = (k, a)
        bones[f"foot.{side}"] = (a, toe)
        feet[side] = {"planted": planted, "clearance": clearance, "angle": angle}
        swing = -0.86 * math.cos(2 * math.pi * phase)
        shoulder = (0.18, y / HIP_HALF_WIDTH * SHOULDER_HALF_WIDTH, 2.59 + bob)
        elbow = (shoulder[0] + 0.52 * math.sin(swing), shoulder[1],
                 shoulder[2] - 0.52 * math.cos(swing))
        wrist = (elbow[0] + 0.49 * math.sin(swing + 1.25), shoulder[1],
                 elbow[2] - 0.49 * math.cos(swing + 1.25))
        bones[f"upper_arm.{side}"] = (shoulder, elbow)
        bones[f"forearm.{side}"] = (elbow, wrist)
        bones[f"hand.{side}"] = (wrist, (wrist[0] + 0.16, wrist[1], wrist[2] + 0.04))
        lift = max(0, knee[1] - 1.05) * 0.25
        bones[f"coat.{side}"] = ((-0.12, y, 1.97 + bob),
                                  (-0.42 - lift, y, 1.28 + bob + lift))
    tail_points = [(-0.37, 0, 1.63), (-0.82, 0, 1.34), (-1.30, 0, 1.27),
                   (-1.75, 0, 1.42), (-2.02, 0, 1.64)]
    tail_points = [(x, y + 0.055 * i * math.sin(2 * math.pi * time - i * 0.5),
                    z + bob + 0.035 * i * math.sin(2 * math.pi * time - i * 0.65))
                   for i, (x, y, z) in enumerate(tail_points)]
    for i in range(4):
        bones[f"tail.{i}"] = (tail_points[i], tail_points[i + 1])
    return {"bones": bones, "feet": feet, "hip_height": hip_z}


def rest_bones():
    bones = {
        "root": ((0, 0, 1.57), (0, 0, 1.81)),
        "torso": ((0, 0, 1.57), (0.18, 0, 2.64)),
        "head": ((0.18, 0, 2.64), (0.28, 0, 3.40)),
    }
    for side, y in (("near", -HIP_HALF_WIDTH), ("far", HIP_HALF_WIDTH)):
        hip = (0, y, 1.57)
        knee = (0.12, y, 1.57 - math.sqrt(THIGH ** 2 - 0.12 ** 2))
        ankle = (0, y, knee[2] - math.sqrt(SHIN ** 2 - 0.12 ** 2))
        bones[f"thigh.{side}"] = (hip, knee)
        bones[f"shin.{side}"] = (knee, ankle)
        bones[f"foot.{side}"] = (ankle, (0.5, y, ankle[2]))
        shoulder = (0.18, y / HIP_HALF_WIDTH * SHOULDER_HALF_WIDTH, 2.59)
        elbow = (0.18, shoulder[1], 2.07)
        wrist = (0.18, shoulder[1], 1.58)
        bones[f"upper_arm.{side}"] = (shoulder, elbow)
        bones[f"forearm.{side}"] = (elbow, wrist)
        bones[f"hand.{side}"] = (wrist, (0.34, wrist[1], wrist[2] + 0.04))
        bones[f"coat.{side}"] = ((-0.12, y, 1.97), (-0.42, y, 1.28))
    for i, (start, end) in enumerate(zip(
            [(-0.37, 0, 1.63), (-0.82, 0, 1.34), (-1.30, 0, 1.27), (-1.75, 0, 1.42)],
            [(-0.82, 0, 1.34), (-1.30, 0, 1.27), (-1.75, 0, 1.42), (-2.02, 0, 1.64)])):
        bones[f"tail.{i}"] = (start, end)
    return bones
