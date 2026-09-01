"""Joint hierarchy and volume shells built from a measurement set.

The rig is 3D on purpose. A per-view 2D drawing system reintroduces exactly the
failure it is meant to prevent: nothing forces the side profile to describe the
same body as the front. Here every view is a projection of one solved skeleton,
so cross-view consistency is structural rather than reviewed.

Axes: +X is the character's own left, +Y is up, +Z is the direction the
character faces. The origin is the floor point between the feet, so a rendered
frame's contact line is y = 0 by construction.
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass

from .math3d import IDENTITY, Mat4, Vec3, euler_xyz, multiply, origin_of, translation
from .measurements import Proportions

# Region tags. They drive per-part colouring in the region map, which is what a
# generator needs in order to be told "this band is the torso, not the cape".
HEAD = "head"
HAIR = "hair"
TORSO = "torso"
ARM = "arm"
HAND = "hand"
LEG = "leg"
FOOT = "foot"
HELMET = "helmet"
ARMOR = "armor"
CAPE = "cape"
WEAPON = "weapon"


@dataclass(frozen=True)
class Joint:
    name: str
    parent: str | None
    offset: Vec3


@dataclass(frozen=True)
class Capsule:
    """A tapered capsule spanning two joints.

    Cross-sections are elliptical: `rx` is the half-width across the body and
    `rz` the half-depth front-to-back. Keeping them separate is what makes a
    side profile narrower than a front view instead of an identical blob.
    """

    name: str
    joint_a: str
    joint_b: str
    rx_a: float
    rz_a: float
    rx_b: float
    rz_b: float
    region: str


@dataclass(frozen=True)
class Ellipsoid:
    """An ellipsoid parented to a joint frame.

    Used where a capsule's spherical caps would overshoot a hard measurement,
    the skull above all: the head must be exactly one head unit tall.
    """

    name: str
    joint: str
    center: Vec3
    rx: float
    ry: float
    rz: float
    region: str


Volume = Capsule | Ellipsoid


@dataclass(frozen=True)
class Rig:
    proportions: Proportions
    joints: tuple[Joint, ...]
    volumes: tuple[Volume, ...]

    def joint(self, name: str) -> Joint:
        for j in self.joints:
            if j.name == name:
                return j
        raise KeyError(f"no joint named {name!r}")


def build_rig(
    p: Proportions,
    extra_joints: Sequence[Joint] = (),
    attachments: Sequence[Volume] = (),
) -> Rig:
    """Build the body rig, optionally wearing a costume.

    Costume pieces arrive as already-sized joints and volumes rather than as a
    costume object, so this module stays independent of `costume.py`; see
    `Costume.build`. Gear that extends past the body — a weapon, a cape — needs
    its own joint so forward kinematics swings it with the limb it hangs from.
    """
    p.validate()

    hip_y = p.inseam
    half_shoulder = p.shoulder_width / 2.0
    # Hip and shoulder sockets sit inboard of the silhouette width; the
    # remaining width comes from the deltoid and gluteal volumes around them.
    hip_socket_x = p.hip_width * 0.28
    thigh = p.thigh_length
    calf = p.calf_length

    joints: list[Joint] = [
        Joint("pelvis", None, (0.0, hip_y, 0.0)),
        Joint("waist", "pelvis", (0.0, p.waist_to_hip, 0.0)),
        Joint("chest", "waist", (0.0, p.shoulder_to_waist, 0.0)),
        Joint("neck", "chest", (0.0, p.neck_length, 0.0)),
        Joint("head", "neck", (0.0, 0.0, 0.0)),
    ]

    for side, sign in (("l", 1.0), ("r", -1.0)):
        joints.extend(
            (
                Joint(f"shoulder_{side}", "chest", (sign * half_shoulder, 0.0, 0.0)),
                Joint(f"elbow_{side}", f"shoulder_{side}", (0.0, -p.upper_arm, 0.0)),
                Joint(f"wrist_{side}", f"elbow_{side}", (0.0, -p.forearm, 0.0)),
                Joint(f"hand_{side}", f"wrist_{side}", (0.0, -p.hand_length, 0.0)),
                Joint(f"hip_{side}", "pelvis", (sign * hip_socket_x, 0.0, 0.0)),
                Joint(f"knee_{side}", f"hip_{side}", (0.0, -thigh, 0.0)),
                Joint(f"ankle_{side}", f"knee_{side}", (0.0, -calf, 0.0)),
                Joint(
                    f"toe_{side}",
                    f"ankle_{side}",
                    (0.0, -p.foot_height * 0.45, p.foot_length * 0.82),
                ),
            )
        )

    volumes: list[Volume] = [
        Capsule(
            "pelvis_block",
            "pelvis",
            "waist",
            p.hip_width / 2.0,
            p.hip_depth / 2.0,
            p.waist_width / 2.0,
            p.waist_depth / 2.0,
            TORSO,
        ),
        Capsule(
            "ribcage",
            "waist",
            "chest",
            p.waist_width / 2.0,
            p.waist_depth / 2.0,
            p.chest_width / 2.0,
            p.chest_depth / 2.0,
            TORSO,
        ),
        Capsule(
            "shoulder_yoke",
            "shoulder_r",
            "shoulder_l",
            p.chest_depth * 0.42,
            p.chest_depth * 0.42,
            p.chest_depth * 0.42,
            p.chest_depth * 0.42,
            TORSO,
        ),
        Capsule(
            "neck_column",
            "chest",
            "neck",
            p.neck_width / 2.0,
            p.neck_width / 2.0,
            p.neck_width / 2.0,
            p.neck_width / 2.0,
            TORSO,
        ),
        Ellipsoid(
            "skull",
            "head",
            (0.0, 0.5, 0.0),
            p.head_width / 2.0,
            0.5,
            p.head_depth / 2.0,
            HEAD,
        ),
        # A brow ridge and a jaw, protruding forward of the skull.
        #
        # Without them the head is a smooth ellipsoid, and a generator handed a
        # smooth ellipsoid draws a smooth ellipsoid: every frame came back with
        # a featureless blob for a face. They also make the head *directional* —
        # a symmetric skull gives no cue which way the character faces, which is
        # why a three-quarter front view was rendered as a back view.
        # Both reach head_depth * 0.60 forward, clearing the skull's 0.50 front
        # by a tenth of a head depth. An earlier attempt cleared it by 0.018
        # head units — about one pixel at 1024 — and was invisible in the depth
        # map, which is the same as not existing.
        Ellipsoid(
            "brow",
            "head",
            (0.0, 0.60, p.head_depth * 0.35),
            p.head_width * 0.38,
            0.11,
            p.head_depth * 0.25,
            HEAD,
        ),
        Ellipsoid(
            "muzzle",
            "head",
            (0.0, 0.38, p.head_depth * 0.34),
            p.head_width * 0.26,
            0.14,
            p.head_depth * 0.26,
            HEAD,
        ),
    ]

    if p.eye_radius > 0.0:
        # Eyes protrude past the skull surface rather than sitting flush on it.
        # A depth map only carries shape, so an eye that does not stick out is
        # not in the picture at all, and the generator invents a blank face.
        for side, sign in (("l", 1.0), ("r", -1.0)):
            volumes.append(
                Ellipsoid(
                    f"eye_{side}",
                    "head",
                    (
                        sign * p.head_width * 0.26,
                        0.55,
                        p.head_depth * 0.34,
                    ),
                    p.eye_radius,
                    p.eye_radius,
                    p.eye_radius,
                    HEAD,
                )
            )

    if p.hair.volume > 1.0 or p.hair.length > 0.0:
        volumes.append(
            Ellipsoid(
                "hair_mass",
                "head",
                (0.0, 0.5 + p.hair.length * 0.18, -p.head_depth * 0.06),
                p.head_width / 2.0 * p.hair.volume,
                0.5 * p.hair.volume,
                p.head_depth / 2.0 * p.hair.volume,
                HAIR,
            )
        )

    for side in ("l", "r"):
        volumes.extend(
            (
                Capsule(
                    f"upper_arm_{side}",
                    f"shoulder_{side}",
                    f"elbow_{side}",
                    p.upper_arm_radius,
                    p.upper_arm_radius,
                    p.elbow_radius,
                    p.elbow_radius,
                    ARM,
                ),
                Capsule(
                    f"forearm_{side}",
                    f"elbow_{side}",
                    f"wrist_{side}",
                    p.elbow_radius,
                    p.elbow_radius,
                    p.wrist_radius,
                    p.wrist_radius,
                    ARM,
                ),
                Capsule(
                    f"hand_{side}",
                    f"wrist_{side}",
                    f"hand_{side}",
                    p.wrist_radius,
                    p.wrist_radius * 0.8,
                    p.hand_radius,
                    p.hand_radius * 0.55,
                    HAND,
                ),
                Capsule(
                    f"thigh_{side}",
                    f"hip_{side}",
                    f"knee_{side}",
                    p.thigh_radius,
                    p.thigh_radius,
                    p.knee_radius,
                    p.knee_radius,
                    LEG,
                ),
                Capsule(
                    f"calf_{side}",
                    f"knee_{side}",
                    f"ankle_{side}",
                    p.knee_radius,
                    p.knee_radius,
                    p.ankle_radius,
                    p.ankle_radius,
                    LEG,
                ),
                Capsule(
                    f"foot_{side}",
                    f"ankle_{side}",
                    f"toe_{side}",
                    p.ankle_radius,
                    p.ankle_radius,
                    p.ankle_radius * 0.7,
                    p.ankle_radius * 0.7,
                    FOOT,
                ),
            )
        )

    joint_names = {j.name for j in joints}
    for extra in extra_joints:
        if extra.parent not in joint_names:
            raise KeyError(
                f"costume joint {extra.name!r} hangs from unknown joint "
                f"{extra.parent!r}"
            )
        if extra.name in joint_names:
            raise KeyError(f"costume joint {extra.name!r} collides with a body joint")
        joints.append(extra)
        joint_names.add(extra.name)

    for attachment in attachments:
        referenced = (
            (attachment.joint,)
            if isinstance(attachment, Ellipsoid)
            else (attachment.joint_a, attachment.joint_b)
        )
        for name in referenced:
            if name not in joint_names:
                raise KeyError(
                    f"attachment {attachment.name!r} references unknown joint "
                    f"{name!r}"
                )
        volumes.append(attachment)

    return Rig(proportions=p, joints=tuple(joints), volumes=tuple(volumes))


def solve(rig: Rig, pose: dict[str, tuple[float, float, float]]) -> dict[str, Mat4]:
    """Forward kinematics. Returns each joint's world transform.

    An unknown joint name in `pose` is an error, not a no-op: a silently ignored
    typo produces a plausible wrong figure, which is the worst outcome for a
    reference the generator is supposed to obey.
    """
    known = {j.name for j in rig.joints}
    unknown = set(pose) - known - {"root"}
    if unknown:
        raise KeyError(f"pose references unknown joints: {sorted(unknown)}")

    world: dict[str, Mat4] = {}
    root_rotation = euler_xyz(*pose.get("root", (0.0, 0.0, 0.0)))

    for joint in rig.joints:
        local = multiply(
            translation(joint.offset), euler_xyz(*pose.get(joint.name, (0.0, 0.0, 0.0)))
        )
        if joint.parent is None:
            world[joint.name] = multiply(multiply(IDENTITY, root_rotation), local)
            continue
        world[joint.name] = multiply(world[joint.parent], local)

    return world


def sole_height(rig: Rig, world: dict[str, Mat4], side: str) -> float:
    """Lowest y of one foot's shell.

    Only the foot capsule is consulted. Scanning every volume for a global
    minimum was the earlier approach and it was wrong: cross-sections are
    defined per projection rather than as closed 3D solids, so a "lowest point
    in 3D" does not exist to be found, and the search picked up whichever shell
    happened to bulge furthest down.
    """
    if side not in ("l", "r"):
        raise ValueError(f"side must be 'l' or 'r', got {side!r}")

    name = f"foot_{side}"
    for volume in rig.volumes:
        if isinstance(volume, Capsule) and volume.name == name:
            return min(
                origin_of(world[volume.joint_a])[1] - max(volume.rx_a, volume.rz_a),
                origin_of(world[volume.joint_b])[1] - max(volume.rx_b, volume.rz_b),
            )
    raise KeyError(f"rig has no {name!r} capsule to seat on")


def seat_on_ground(
    rig: Rig, world: dict[str, Mat4], planted_foot: str | None
) -> dict[str, Mat4]:
    """Translate a solved pose so the planted foot's sole rests on y = 0.

    Registration is the rig's job, not the image processor's. The idle pilot
    failed review on a three-pixel horizontal oscillation introduced by
    per-frame registration downstream; a pose that arrives already seated
    removes that class of drift.

    `planted_foot` of None means airborne. An airborne frame keeps its authored
    root height and is deliberately not seated — seating a jump would plant it
    on the floor and destroy the only thing that reads as airborne.
    """
    if planted_foot is None:
        return world

    if planted_foot == "both":
        sides = ("l", "r")
    elif planted_foot in ("l", "r"):
        sides = (planted_foot,)
    else:
        raise ValueError(
            f"planted_foot must be 'l', 'r', 'both', or None, got {planted_foot!r}"
        )

    drop = min(sole_height(rig, world, side) for side in sides)
    lift = translation((0.0, -drop, 0.0))
    return {name: multiply(lift, m) for name, m in world.items()}


def lowest_shell(rig: Rig, world: dict[str, Mat4]) -> float:
    """Lowest y reached by any shell, for validation only.

    Not for seating — that is `seat_on_ground`, which uses the planted foot the
    pose names. This scan exists to catch a pose that drives a limb through the
    floor, which is easy to author by accident: angling the lead leg forward
    drops the pelvis, and a trailing leg that looked fine standing then
    penetrates.
    """
    lowest = float("inf")
    for volume in rig.volumes:
        if isinstance(volume, Capsule):
            lowest = min(
                lowest,
                origin_of(world[volume.joint_a])[1] - max(volume.rx_a, volume.rz_a),
                origin_of(world[volume.joint_b])[1] - max(volume.rx_b, volume.rz_b),
            )
            continue
        center = origin_of(multiply(world[volume.joint], translation(volume.center)))
        lowest = min(lowest, center[1] - volume.ry)
    return lowest


def skeletal_height(rig: Rig, world: dict[str, Mat4]) -> float:
    """Crown of the skull above y = 0, in head units.

    This is the number that must match `heads_tall`, and the one the head-unit
    grid is drawn against. Hair and a helmet legitimately rise above it, so
    `shell_height` will read taller on most figures and is not a contradiction.
    """
    for volume in rig.volumes:
        if isinstance(volume, Ellipsoid) and volume.name == "skull":
            center = origin_of(
                multiply(world[volume.joint], translation(volume.center))
            )
            return center[1] + volume.ry
    raise KeyError("rig has no 'skull' volume to measure against")


def shell_height(rig: Rig, world: dict[str, Mat4]) -> float:
    """Height of the tallest shell above y = 0, hair and gear included.

    This is what the rendered silhouette occupies, so it is what has to fit the
    canvas — but it is not the figure's stated height. See `skeletal_height`.
    """
    tallest = float("-inf")
    for volume in rig.volumes:
        if isinstance(volume, Capsule):
            tallest = max(
                tallest,
                origin_of(world[volume.joint_a])[1] + max(volume.rx_a, volume.rz_a),
                origin_of(world[volume.joint_b])[1] + max(volume.rx_b, volume.rz_b),
            )
            continue
        center = origin_of(multiply(world[volume.joint], translation(volume.center)))
        tallest = max(tallest, center[1] + volume.ry)
    return tallest
