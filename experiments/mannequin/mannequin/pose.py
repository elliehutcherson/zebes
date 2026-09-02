"""Pose library and cycle interpolation.

A pose is a plain mapping of joint name to (rx, ry, rz) Euler degrees, plus the
pseudo-joint `root` for whole-body lean and turn. Sign conventions, given a
character facing +Z with limbs hanging along -Y:

* negative `rx` swings a limb forward, positive swings it back;
* positive `rz` moves a limb toward +X, the character's own left, so an
  outward spread is positive on the left side and negative on the right;
* `ry` is twist about the limb's own axis.

Cycles are authored as a few keys and interpolated. Generating twelve frames
from four keys is what makes a walk read as one motion: the pose-conditioned
pilots produced frames that were individually plausible and collectively
incoherent because each was drawn independently.
"""

from __future__ import annotations

from dataclasses import dataclass

Pose = dict[str, tuple[float, float, float]]


def mirror(pose: Pose) -> Pose:
    """Swap left and right and negate the lateral and twist components."""
    mirrored: Pose = {}
    for name, (rx, ry, rz) in pose.items():
        if name.endswith("_l"):
            target = name[:-2] + "_r"
        elif name.endswith("_r"):
            target = name[:-2] + "_l"
        else:
            target = name
        mirrored[target] = (rx, -ry, -rz)
    return mirrored


def blend(a: Pose, b: Pose, t: float) -> Pose:
    """Linear blend. Euler interpolation is adequate for the ranges used here.

    Limb angles stay well under the wrap-around and gimbal ranges where slerp
    would matter; if a pose ever needs more than about 120 degrees on two axes
    at once, replace this rather than tuning around it.
    """
    names = set(a) | set(b)
    out: Pose = {}
    for name in names:
        ax, ay, az = a.get(name, (0.0, 0.0, 0.0))
        bx, by, bz = b.get(name, (0.0, 0.0, 0.0))
        out[name] = (
            ax + (bx - ax) * t,
            ay + (by - ay) * t,
            az + (bz - az) * t,
        )
    return out


T_POSE: Pose = {
    "shoulder_l": (0.0, 0.0, 90.0),
    "shoulder_r": (0.0, 0.0, -90.0),
}

A_POSE: Pose = {
    "shoulder_l": (0.0, 0.0, 14.0),
    "shoulder_r": (0.0, 0.0, -14.0),
    "elbow_l": (-4.0, 0.0, 0.0),
    "elbow_r": (-4.0, 0.0, 0.0),
    "hip_l": (0.0, 0.0, 3.0),
    "hip_r": (0.0, 0.0, -3.0),
}

IDLE_A: Pose = {
    "root": (-2.0, 0.0, 0.0),
    "shoulder_l": (-4.0, 0.0, 9.0),
    "shoulder_r": (-4.0, 0.0, -9.0),
    "elbow_l": (-16.0, 0.0, 0.0),
    "elbow_r": (-16.0, 0.0, 0.0),
    "waist": (1.0, 0.0, 0.0),
    "hip_l": (0.0, 0.0, 4.0),
    "hip_r": (-1.0, 0.0, -4.0),
    "knee_l": (3.0, 0.0, 0.0),
    "knee_r": (5.0, 0.0, 0.0),
}

IDLE_B: Pose = {
    "root": (-1.0, 0.0, 0.0),
    "shoulder_l": (-2.0, 0.0, 11.0),
    "shoulder_r": (-2.0, 0.0, -11.0),
    "elbow_l": (-13.0, 0.0, 0.0),
    "elbow_r": (-13.0, 0.0, 0.0),
    "waist": (-1.0, 0.0, 0.0),
    "hip_l": (0.0, 0.0, 4.0),
    "hip_r": (0.0, 0.0, -4.0),
    "knee_l": (1.0, 0.0, 0.0),
    "knee_r": (2.0, 0.0, 0.0),
}

CROUCH: Pose = {
    "root": (-14.0, 0.0, 0.0),
    "waist": (6.0, 0.0, 0.0),
    "hip_l": (-62.0, 0.0, 7.0),
    "hip_r": (-62.0, 0.0, -7.0),
    "knee_l": (84.0, 0.0, 0.0),
    "knee_r": (84.0, 0.0, 0.0),
    "ankle_l": (-26.0, 0.0, 0.0),
    "ankle_r": (-26.0, 0.0, 0.0),
    "shoulder_l": (-24.0, 0.0, 12.0),
    "shoulder_r": (-24.0, 0.0, -12.0),
    "elbow_l": (-42.0, 0.0, 0.0),
    "elbow_r": (-42.0, 0.0, 0.0),
}

JUMP_RISE: Pose = {
    "root": (-6.0, 0.0, 0.0),
    "hip_l": (-34.0, 0.0, 6.0),
    "hip_r": (10.0, 0.0, -6.0),
    "knee_l": (52.0, 0.0, 0.0),
    "knee_r": (16.0, 0.0, 0.0),
    "ankle_l": (-10.0, 0.0, 0.0),
    "ankle_r": (-24.0, 0.0, 0.0),
    "shoulder_l": (-52.0, 0.0, 16.0),
    "shoulder_r": (-70.0, 0.0, -18.0),
    "elbow_l": (-38.0, 0.0, 0.0),
    "elbow_r": (-30.0, 0.0, 0.0),
}

JUMP_FALL: Pose = {
    "root": (4.0, 0.0, 0.0),
    "hip_l": (-16.0, 0.0, 8.0),
    "hip_r": (18.0, 0.0, -8.0),
    "knee_l": (28.0, 0.0, 0.0),
    "knee_r": (40.0, 0.0, 0.0),
    "shoulder_l": (26.0, 0.0, 26.0),
    "shoulder_r": (30.0, 0.0, -24.0),
    "elbow_l": (-24.0, 0.0, 0.0),
    "elbow_r": (-20.0, 0.0, 0.0),
}

# Run cycle keys for a right-foot-forward contact. The opposite half of the
# cycle is the mirror of these, which is what guarantees the two halves describe
# the same body rather than two independently drawn ones.
RUN_CONTACT: Pose = {
    "root": (-9.0, 0.0, 0.0),
    "waist": (0.0, -5.0, 0.0),
    "chest": (0.0, 6.0, 0.0),
    "hip_r": (-30.0, 0.0, -4.0),
    "knee_r": (12.0, 0.0, 0.0),
    "ankle_r": (-6.0, 0.0, 0.0),
    # The trailing knee is well bent rather than extended. The lead leg reaches
    # forward at 30 degrees, which drops the pelvis when the figure is seated on
    # that foot; a near-straight trailing leg then reaches a third of a head
    # unit through the floor. It has also just pushed off, so it should be
    # folding.
    "hip_l": (26.0, 0.0, 4.0),
    "knee_l": (62.0, 0.0, 0.0),
    "ankle_l": (-16.0, 0.0, 0.0),
    "shoulder_l": (-44.0, 0.0, 12.0),
    "elbow_l": (-74.0, 0.0, 0.0),
    "shoulder_r": (34.0, 0.0, -12.0),
    "elbow_r": (-56.0, 0.0, 0.0),
}

RUN_PASSING: Pose = {
    "root": (-11.0, 0.0, 0.0),
    "waist": (0.0, -2.0, 0.0),
    "chest": (0.0, 3.0, 0.0),
    "hip_r": (-4.0, 0.0, -3.0),
    "knee_r": (18.0, 0.0, 0.0),
    "ankle_r": (-4.0, 0.0, 0.0),
    "hip_l": (-18.0, 0.0, 3.0),
    "knee_l": (76.0, 0.0, 0.0),
    "ankle_l": (-18.0, 0.0, 0.0),
    "shoulder_l": (-20.0, 0.0, 11.0),
    "elbow_l": (-66.0, 0.0, 0.0),
    "shoulder_r": (12.0, 0.0, -11.0),
    "elbow_r": (-62.0, 0.0, 0.0),
}


@dataclass(frozen=True)
class Frame:
    """One posed frame plus the contract data the sprite pipeline needs.

    `planted_foot` is authored here rather than measured downstream because the
    rig knows which foot is carrying weight; a processor can only guess from
    pixels.
    """

    index: int
    label: str
    pose: Pose
    planted_foot: str | None


def run_cycle(frames: int = 12) -> tuple[Frame, ...]:
    """Interpolate a full run over `frames`, right contact first.

    An even count is required: the second half is the mirror of the first, and
    an odd count would place the mirror boundary inside a key.
    """
    if frames < 4 or frames % 2 != 0:
        raise ValueError(f"run_cycle needs an even frame count of at least 4, got {frames}")

    keys = (
        ("contact-r", RUN_CONTACT, "r"),
        ("passing-r", RUN_PASSING, "r"),
        ("contact-l", mirror(RUN_CONTACT), "l"),
        ("passing-l", mirror(RUN_PASSING), "l"),
    )

    out: list[Frame] = []
    per_key = frames / len(keys)
    for i in range(frames):
        position = i / per_key
        key_index = int(position) % len(keys)
        next_index = (key_index + 1) % len(keys)
        t = position - int(position)
        label, pose_a, planted = keys[key_index]
        _, pose_b, _ = keys[next_index]
        out.append(
            Frame(
                index=i,
                label=label if t < 1e-9 else f"{label}+{t:.2f}",
                pose=blend(pose_a, pose_b, t),
                planted_foot=planted,
            )
        )
    return tuple(out)


def idle_cycle(frames: int = 4) -> tuple[Frame, ...]:
    """A breathing idle that returns to its start, so the loop has no pop."""
    if frames < 2:
        raise ValueError(f"idle_cycle needs at least 2 frames, got {frames}")
    out: list[Frame] = []
    for i in range(frames):
        # Triangle wave: out to IDLE_B at the midpoint and back to IDLE_A.
        phase = i / frames
        t = 2.0 * phase if phase <= 0.5 else 2.0 * (1.0 - phase)
        out.append(
            Frame(
                index=i,
                label=f"idle-{i}",
                pose=blend(IDLE_A, IDLE_B, t),
                planted_foot="both",
            )
        )
    return tuple(out)


# Single poses carry their planted foot for the same reason cycle frames do:
# seating is meaningless without knowing which foot holds the weight, and the
# two jump poses must stay off the ground.
STATIC_POSES: dict[str, Frame] = {
    name: Frame(index=0, label=name, pose=pose, planted_foot=planted)
    for name, pose, planted in (
        ("t-pose", T_POSE, "both"),
        ("a-pose", A_POSE, "both"),
        ("idle", IDLE_A, "both"),
        ("crouch", CROUCH, "both"),
        ("jump-rise", JUMP_RISE, None),
        ("jump-fall", JUMP_FALL, None),
        ("run-contact", RUN_CONTACT, "r"),
        ("run-passing", RUN_PASSING, "r"),
    )
}


def static_pose(name: str) -> Frame:
    if name not in STATIC_POSES:
        raise KeyError(f"unknown pose {name!r}; available: {sorted(STATIC_POSES)}")
    return STATIC_POSES[name]
