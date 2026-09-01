"""Costume and equipment volumes layered onto a body rig.

Kept separate from `Proportions` so one body wears several kits: the measurement
set describes a person, a costume describes what that person is wearing, and
changing the kit must not silently change the figure underneath.

This exists because of a specific failure. The rejected pose-conditioned pilots
drifted on *helmet size* and *belt detail*, not only on body mass — see
`docs/history/animation-pose-conditioned-experiment.md`. A body-only guide
constrains neither, so helmet and belt widths are measured by the drift gate
alongside the shoulders.

Every dimension is in head units, so a kit fits any figure that wears it.
"""

from __future__ import annotations

from dataclasses import dataclass

from .measurements import Proportions
from .skeleton import (
    ARMOR,
    CAPE,
    HEAD,
    HELMET,
    WEAPON,
    Capsule,
    Ellipsoid,
    Joint,
    Volume,
)


class CostumeError(ValueError):
    """Raised when a costume piece cannot describe a coherent volume."""


@dataclass(frozen=True)
class Helmet:
    """Encloses the skull.

    `coverage` scales the skull's own semi-axes, so 1.0 is a skin-tight cap and
    anything above adds shell thickness. `crest` raises the top only, which is
    what makes a helmet silhouette read as taller than a head.
    """

    coverage: float
    crest: float


@dataclass(frozen=True)
class Pauldrons:
    radius: float
    drop: float


@dataclass(frozen=True)
class Belt:
    width_scale: float
    thickness: float


@dataclass(frozen=True)
class Backpack:
    width_scale: float
    height: float
    depth: float


@dataclass(frozen=True)
class Cape:
    """A slab hanging behind the chest. Reads as a cape in profile and in back."""

    length: float
    width_scale: float
    thickness: float


@dataclass(frozen=True)
class Ears:
    """A swept-back pair, parented to the head so they turn with it.

    Built from joints and capsules rather than offset ellipsoids because an
    ellipsoid's axes stay world-aligned here, so it cannot be angled; ears that
    sweep back and down need real direction.
    """

    length: float
    droop: float
    thickness: float


@dataclass(frozen=True)
class Tail:
    length: float
    droop: float
    thickness: float


@dataclass(frozen=True)
class Weapon:
    """Held in one hand, running along the hand's own axis.

    Given its own joints rather than an offset volume so forward kinematics
    swings it with the arm. An offset ellipsoid would stay stubbornly vertical
    while the arm rotated.

    `grip` is the fraction of the shaft above the hand, so a staff or polearm is
    held partway along its length. Without it a long weapon extends only
    downward from the fist and spears straight through the floor.
    """

    hand: str
    length: float
    radius: float
    grip: float = 0.0


@dataclass(frozen=True)
class Costume:
    name: str
    helmet: Helmet | None = None
    pauldrons: Pauldrons | None = None
    belt: Belt | None = None
    backpack: Backpack | None = None
    cape: Cape | None = None
    weapon: Weapon | None = None
    ears: Ears | None = None
    tail: Tail | None = None

    def build(self, p: Proportions) -> tuple[tuple[Joint, ...], tuple[Volume, ...]]:
        """Size this kit against a measurement set.

        Returns the extra joints and the attachment volumes, in the order
        `skeleton.build_rig` expects them.
        """
        joints: list[Joint] = []
        volumes: list[Volume] = []

        if self.helmet is not None:
            volumes.append(_helmet_volume(p, self.helmet))
        if self.pauldrons is not None:
            volumes.extend(_pauldron_volumes(self.pauldrons))
        if self.belt is not None:
            volumes.append(_belt_volume(p, self.belt))
        if self.backpack is not None:
            volumes.append(_backpack_volume(p, self.backpack))
        if self.cape is not None:
            volumes.append(_cape_volume(p, self.cape))
        if self.weapon is not None:
            weapon_joints, weapon_volume = _weapon_pieces(self.weapon)
            joints.extend(weapon_joints)
            volumes.append(weapon_volume)
        if self.ears is not None:
            ear_joints, ear_volumes = _ear_pieces(p, self.ears)
            joints.extend(ear_joints)
            volumes.extend(ear_volumes)
        if self.tail is not None:
            tail_joint, tail_volume = _tail_pieces(p, self.tail)
            joints.append(tail_joint)
            volumes.append(tail_volume)

        return tuple(joints), tuple(volumes)


def _helmet_volume(p: Proportions, helmet: Helmet) -> Ellipsoid:
    if helmet.coverage < 1.0:
        raise CostumeError(
            f"helmet coverage {helmet.coverage} would sink inside the skull; "
            "use 1.0 or more"
        )
    if helmet.crest < 0.0:
        raise CostumeError(f"helmet crest must not be negative, got {helmet.crest}")

    half_height = 0.5 * helmet.coverage + helmet.crest * 0.5
    return Ellipsoid(
        name="helmet",
        joint="head",
        center=(0.0, 0.5 + helmet.crest * 0.5, 0.0),
        rx=p.head_width / 2.0 * helmet.coverage,
        ry=half_height,
        rz=p.head_depth / 2.0 * helmet.coverage,
        region=HELMET,
    )


def _pauldron_volumes(pauldrons: Pauldrons) -> tuple[Ellipsoid, Ellipsoid]:
    if pauldrons.radius <= 0.0:
        raise CostumeError(f"pauldron radius must be positive, got {pauldrons.radius}")
    return tuple(  # type: ignore[return-value]
        Ellipsoid(
            name=f"pauldron_{side}",
            joint=f"shoulder_{side}",
            center=(sign * pauldrons.radius * 0.3, -pauldrons.drop, 0.0),
            rx=pauldrons.radius,
            ry=pauldrons.radius,
            rz=pauldrons.radius,
            region=ARMOR,
        )
        for side, sign in (("l", 1.0), ("r", -1.0))
    )


def _belt_volume(p: Proportions, belt: Belt) -> Ellipsoid:
    if belt.width_scale < 1.0:
        raise CostumeError(
            f"belt width_scale {belt.width_scale} would sink inside the waist"
        )
    if belt.thickness <= 0.0:
        raise CostumeError(f"belt thickness must be positive, got {belt.thickness}")
    return Ellipsoid(
        name="belt",
        joint="pelvis",
        center=(0.0, p.waist_to_hip * 0.55, 0.0),
        rx=p.waist_width / 2.0 * belt.width_scale,
        ry=belt.thickness,
        rz=p.waist_depth / 2.0 * belt.width_scale,
        region=ARMOR,
    )


def _backpack_volume(p: Proportions, pack: Backpack) -> Ellipsoid:
    if pack.depth <= 0.0 or pack.height <= 0.0:
        raise CostumeError("backpack height and depth must be positive")
    return Ellipsoid(
        name="backpack",
        joint="chest",
        center=(0.0, -pack.height * 0.4, -(p.chest_depth / 2.0 + pack.depth * 0.5)),
        rx=p.chest_width / 2.0 * pack.width_scale,
        ry=pack.height * 0.5,
        rz=pack.depth * 0.5,
        region=ARMOR,
    )


def _cape_volume(p: Proportions, cape: Cape) -> Ellipsoid:
    if cape.length <= 0.0:
        raise CostumeError(f"cape length must be positive, got {cape.length}")
    return Ellipsoid(
        name="cape",
        joint="chest",
        center=(0.0, -cape.length * 0.5, -(p.chest_depth / 2.0 + cape.thickness)),
        rx=p.chest_width / 2.0 * cape.width_scale,
        ry=cape.length * 0.5,
        rz=cape.thickness,
        region=CAPE,
    )


def _weapon_pieces(weapon: Weapon) -> tuple[tuple[Joint, ...], Capsule]:
    if weapon.hand not in ("l", "r"):
        raise CostumeError(f"weapon hand must be 'l' or 'r', got {weapon.hand!r}")
    if weapon.length <= 0.0 or weapon.radius <= 0.0:
        raise CostumeError("weapon length and radius must be positive")
    if not 0.0 <= weapon.grip < 1.0:
        raise CostumeError(f"weapon grip must be in [0, 1), got {weapon.grip}")

    hand = f"hand_{weapon.hand}"
    butt = f"weapon_butt_{weapon.hand}"
    tip = f"weapon_tip_{weapon.hand}"
    return (
        (
            Joint(butt, hand, (0.0, -weapon.length * (1.0 - weapon.grip), 0.0)),
            Joint(tip, hand, (0.0, weapon.length * weapon.grip, 0.0)),
        ),
        Capsule(
            name="weapon",
            joint_a=butt,
            joint_b=tip,
            rx_a=weapon.radius * 0.6,
            rz_a=weapon.radius * 0.6,
            rx_b=weapon.radius,
            rz_b=weapon.radius,
            region=WEAPON,
        ),
    )


def _ear_pieces(p: Proportions, ears: Ears) -> tuple[tuple[Joint, ...], list[Capsule]]:
    if ears.length <= 0.0 or ears.thickness <= 0.0:
        raise CostumeError("ear length and thickness must be positive")

    joints: list[Joint] = []
    volumes: list[Capsule] = []
    for side, sign in (("l", 1.0), ("r", -1.0)):
        base = f"ear_{side}"
        tip = f"ear_tip_{side}"
        joints.append(Joint(base, "head", (sign * p.head_width * 0.44, 0.58, 0.0)))
        joints.append(Joint(tip, base, (sign * ears.length * 0.25, -ears.droop, -ears.length)))
        volumes.append(
            Capsule(
                name=f"ear_{side}",
                joint_a=base,
                joint_b=tip,
                rx_a=ears.thickness,
                rz_a=ears.thickness * 1.4,
                rx_b=ears.thickness * 0.5,
                rz_b=ears.thickness * 0.7,
                region=HEAD,
            )
        )
    return tuple(joints), volumes


def _tail_pieces(p: Proportions, tail: Tail) -> tuple[Joint, Capsule]:
    if tail.length <= 0.0 or tail.thickness <= 0.0:
        raise CostumeError("tail length and thickness must be positive")
    return (
        Joint("tail_tip", "pelvis", (0.0, -tail.droop, -tail.length)),
        Capsule(
            name="tail",
            joint_a="pelvis",
            joint_b="tail_tip",
            rx_a=tail.thickness,
            rz_a=tail.thickness,
            rx_b=tail.thickness * 0.35,
            rz_b=tail.thickness * 0.35,
            region=ARMOR,
        ),
    )


KNIGHT = Costume(
    name="knight",
    helmet=Helmet(coverage=1.16, crest=0.12),
    pauldrons=Pauldrons(radius=0.26, drop=0.04),
    belt=Belt(width_scale=1.12, thickness=0.09),
    cape=Cape(length=1.9, width_scale=1.05, thickness=0.05),
    # A held weapon has to exist in the depth map or the generator invents one.
    # Left out of the guide, a forward-reaching empty hand produced a malformed
    # double-bladed sword: the arm was constrained and the blade was not.
    weapon=Weapon(hand="l", length=1.5, radius=0.055),
)

SCOUT = Costume(
    name="scout",
    helmet=Helmet(coverage=1.06, crest=0.0),
    belt=Belt(width_scale=1.08, thickness=0.07),
    backpack=Backpack(width_scale=0.8, height=0.9, depth=0.34),
)

# The amphibian trickster kit: swept ear-fins, a tail, and a trident held
# partway up the shaft. No helmet — the head's own eyes and muzzle carry it,
# and a helmet ellipsoid would bury them the way it did on the knight.
TRICKSTER = Costume(
    name="trickster",
    belt=Belt(width_scale=1.10, thickness=0.05),
    ears=Ears(length=0.62, droop=0.30, thickness=0.10),
    tail=Tail(length=0.55, droop=0.12, thickness=0.11),
    # Held high on the shaft. In an A-pose the hand hangs at 0.78 head units,
    # so at most 0.73 can sit below it before the butt goes through the floor,
    # and the whole figure plus shaft has to stay inside the 3.57 head units the
    # layout reserves.
    weapon=Weapon(hand="r", length=3.0, radius=0.11, grip=0.78),
)

COSTUMES: dict[str, Costume] = {c.name: c for c in (KNIGHT, SCOUT, TRICKSTER)}


def costume(name: str) -> Costume:
    if name not in COSTUMES:
        raise CostumeError(f"unknown costume {name!r}; available: {sorted(COSTUMES)}")
    return COSTUMES[name]
