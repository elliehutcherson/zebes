"""Character proportions expressed in head units.

Every length is a multiple of one head height (HU), never centimetres. A sprite
pipeline cares about ratios, not absolute anthropometry: the same figure has to
read correctly whether it is rendered at 1024 px for a generator or 48 px for
the atlas, and only the ratios survive that trip.

The vertical measurements are deliberately over-determined the way a garment
chart is: `heads_tall` is also the sum of the stacked segments. `validate`
enforces that closure and fails rather than silently rescaling, because a figure
whose parts do not add up to its height is exactly the drift this tool exists to
detect.
"""

from __future__ import annotations

from dataclasses import dataclass, replace

# A figure whose stacked segments miss its stated height by more than this is
# rejected. One hundredth of a head is under half a pixel on a 48 px sprite.
CLOSURE_TOLERANCE = 1e-2


class ProportionError(ValueError):
    """Raised when a measurement set cannot describe a coherent figure."""


@dataclass(frozen=True)
class Hair:
    """Hair as a silhouette volume, not strands.

    `length` is measured down from the skull top along the back of the head;
    `volume` scales the skull radius outward. Both exist so the generator sees a
    stable head silhouette, which is where the pose-conditioned pilots drifted.
    """

    length: float
    volume: float
    tail_length: float


@dataclass(frozen=True)
class Proportions:
    """One character's measurement set, all values in head units."""

    name: str
    heads_tall: float

    head_width: float
    head_depth: float
    # Eyes are volumes, not decoration. A head with no eye geometry comes back
    # from the generator as a featureless bag every single time, so the size of
    # the eyes is a measurement like any other. Radius in head units; 0 means a
    # face with no eye relief.
    eye_radius: float

    neck_length: float
    neck_width: float

    shoulder_width: float
    chest_width: float
    chest_depth: float
    waist_width: float
    waist_depth: float
    hip_width: float
    hip_depth: float

    shoulder_to_waist: float
    waist_to_hip: float

    inseam: float
    thigh_share: float
    foot_height: float
    foot_length: float

    upper_arm: float
    forearm: float
    hand_length: float

    upper_arm_radius: float
    elbow_radius: float
    wrist_radius: float
    hand_radius: float
    thigh_radius: float
    knee_radius: float
    ankle_radius: float

    hair: Hair

    @property
    def thigh_length(self) -> float:
        return (self.inseam - self.foot_height) * self.thigh_share

    @property
    def calf_length(self) -> float:
        return (self.inseam - self.foot_height) * (1.0 - self.thigh_share)

    @property
    def arm_span(self) -> float:
        """Fingertip to fingertip. A classical figure's span equals its height."""
        return self.shoulder_width + 2.0 * (
            self.upper_arm + self.forearm + self.hand_length
        )

    @property
    def stacked_height(self) -> float:
        return (
            1.0
            + self.neck_length
            + self.shoulder_to_waist
            + self.waist_to_hip
            + self.inseam
        )

    def validate(self) -> None:
        if self.heads_tall <= 0.0:
            raise ProportionError(f"{self.name}: heads_tall must be positive")

        drift = abs(self.stacked_height - self.heads_tall)
        if drift > CLOSURE_TOLERANCE:
            raise ProportionError(
                f"{self.name}: stacked segments total {self.stacked_height:.4f} HU "
                f"but heads_tall is {self.heads_tall:.4f} HU (off by {drift:.4f}). "
                "Adjust a segment or heads_tall; the tool will not rescale for you."
            )

        if not 0.0 < self.thigh_share < 1.0:
            raise ProportionError(f"{self.name}: thigh_share must be in (0, 1)")

        if self.foot_height >= self.inseam:
            raise ProportionError(
                f"{self.name}: foot_height {self.foot_height} must be under "
                f"inseam {self.inseam}"
            )

        for field_name in (
            "head_width",
            "head_depth",
            "neck_length",
            "shoulder_width",
            "chest_width",
            "hip_width",
            "upper_arm",
            "forearm",
            "foot_length",
        ):
            if getattr(self, field_name) <= 0.0:
                raise ProportionError(f"{self.name}: {field_name} must be positive")

        if self.hip_width > self.shoulder_width * 1.6:
            raise ProportionError(
                f"{self.name}: hip_width {self.hip_width} exceeds a plausible "
                f"multiple of shoulder_width {self.shoulder_width}"
            )

    def close_to_height(self) -> "Proportions":
        """Return a copy whose `heads_tall` is the stacked total.

        Use when authoring a new figure by segment; never inside the render path,
        where a mismatch is a bug worth failing on.
        """
        return replace(self, heads_tall=self.stacked_height)


HEROIC_6H = Proportions(
    name="heroic-6h",
    heads_tall=6.0,
    head_width=0.78,
    head_depth=0.86,
    eye_radius=0.075,
    neck_length=0.22,
    neck_width=0.34,
    shoulder_width=1.72,
    chest_width=1.30,
    chest_depth=0.72,
    waist_width=0.98,
    waist_depth=0.62,
    hip_width=1.18,
    hip_depth=0.70,
    shoulder_to_waist=1.06,
    waist_to_hip=0.52,
    inseam=3.20,
    thigh_share=0.52,
    foot_height=0.16,
    foot_length=0.82,
    upper_arm=0.98,
    forearm=0.86,
    hand_length=0.62,
    upper_arm_radius=0.20,
    elbow_radius=0.155,
    wrist_radius=0.115,
    hand_radius=0.16,
    thigh_radius=0.27,
    knee_radius=0.195,
    ankle_radius=0.13,
    hair=Hair(length=0.34, volume=1.14, tail_length=0.0),
)

REALISTIC_75H = Proportions(
    name="realistic-7.5h",
    heads_tall=7.5,
    head_width=0.72,
    head_depth=0.84,
    eye_radius=0.070,
    neck_length=0.28,
    neck_width=0.32,
    shoulder_width=1.85,
    chest_width=1.36,
    chest_depth=0.74,
    waist_width=1.06,
    waist_depth=0.64,
    hip_width=1.26,
    hip_depth=0.72,
    shoulder_to_waist=1.34,
    waist_to_hip=0.66,
    inseam=4.22,
    thigh_share=0.51,
    foot_height=0.18,
    foot_length=0.96,
    upper_arm=1.28,
    forearm=1.10,
    hand_length=0.74,
    upper_arm_radius=0.185,
    elbow_radius=0.145,
    wrist_radius=0.105,
    hand_radius=0.15,
    thigh_radius=0.255,
    knee_radius=0.185,
    ankle_radius=0.12,
    hair=Hair(length=0.30, volume=1.10, tail_length=0.0),
)

CHIBI_4H = Proportions(
    name="chibi-4h",
    heads_tall=4.0,
    head_width=0.94,
    head_depth=0.94,
    eye_radius=0.130,
    neck_length=0.10,
    neck_width=0.30,
    shoulder_width=1.10,
    chest_width=0.92,
    chest_depth=0.58,
    waist_width=0.84,
    waist_depth=0.54,
    hip_width=0.90,
    hip_depth=0.58,
    shoulder_to_waist=0.62,
    waist_to_hip=0.34,
    inseam=1.94,
    thigh_share=0.50,
    foot_height=0.14,
    foot_length=0.62,
    upper_arm=0.60,
    forearm=0.54,
    hand_length=0.40,
    upper_arm_radius=0.17,
    elbow_radius=0.145,
    wrist_radius=0.115,
    hand_radius=0.16,
    thigh_radius=0.24,
    knee_radius=0.185,
    ankle_radius=0.135,
    hair=Hair(length=0.44, volume=1.22, tail_length=0.0),
)

# Which measurements an acceptance gate may test, and which only steer authoring.
#
# The split is forced by resolution, not preference. At the 44 px character
# height in docs/animation-artwork-pipeline.md, one head unit is 7.3 px: inseam
# lands at 23.5 px and shoulder width at 12.6 px, but wrist radius is 0.8 px and
# elbow radius 1.1 px. A gate cannot fail a frame over a measurement that spans
# less than a pixel in the shipped asset. The authoring-only measurements still
# matter, because generation happens near 1024 px and the silhouette they shape
# is what gets downsampled — they are simply not evidence.
GATE_TESTABLE_FIELDS = frozenset(
    {
        "heads_tall",
        "head_width",
        "shoulder_width",
        "chest_width",
        "waist_width",
        "hip_width",
        "inseam",
        "shoulder_to_waist",
        "waist_to_hip",
        "upper_arm",
        "forearm",
        "foot_length",
    }
)

AUTHORING_ONLY_FIELDS = frozenset(
    {
        "head_depth",
        "neck_length",
        "neck_width",
        "chest_depth",
        "waist_depth",
        "hip_depth",
        "foot_height",
        "hand_length",
        "upper_arm_radius",
        "elbow_radius",
        "wrist_radius",
        "hand_radius",
        "thigh_radius",
        "knee_radius",
        "ankle_radius",
    }
)

# One pixel of visible extent. Below this a measurement cannot be evidence.
GATE_RESOLUTION_FLOOR_PX = 1.0


def resolution_report(
    p: Proportions, character_height_px: float
) -> dict[str, tuple[float, float, bool]]:
    """Measurement extents at a render height, and whether each clears a pixel.

    Returns field name to (head units, pixels, clears the floor). Use it to
    check that a chosen render size can actually carry the gate-testable tier
    before trusting a gate result from it.
    """
    if character_height_px <= 0.0:
        raise ProportionError(
            f"character_height_px must be positive, got {character_height_px}"
        )

    pixels_per_head = character_height_px / p.heads_tall
    report: dict[str, tuple[float, float, bool]] = {}
    for field_name in sorted(GATE_TESTABLE_FIELDS | AUTHORING_ONLY_FIELDS):
        value = getattr(p, field_name)
        pixels = value * pixels_per_head
        report[field_name] = (value, pixels, pixels >= GATE_RESOLUTION_FLOOR_PX)
    return report


# An original small amphibian trickster, proportioned from a reference the user
# supplied. Roughly three heads tall: the skull is about a third of the figure,
# the torso is short, and the legs are shorter still. The reference character
# itself is someone else's property, so nothing here copies its design — only
# the proportions and archetype, which is what the pipeline needs.
#
# The distinctive numbers are the head, which is as wide as it is tall; the
# hands and feet, which are large the way a stylised creature's are; and the
# limbs, which are thin enough that the hands read as separate shapes rather
# than as the ends of the arms.
TRICKSTER_3H = Proportions(
    name="trickster-3h",
    heads_tall=3.0,
    head_width=1.02,
    head_depth=1.00,
    eye_radius=0.190,
    neck_length=0.08,
    neck_width=0.26,
    shoulder_width=0.86,
    chest_width=0.66,
    chest_depth=0.48,
    waist_width=0.58,
    waist_depth=0.44,
    hip_width=0.66,
    hip_depth=0.48,
    shoulder_to_waist=0.52,
    waist_to_hip=0.28,
    inseam=1.12,
    thigh_share=0.46,
    foot_height=0.10,
    foot_length=0.74,
    upper_arm=0.46,
    forearm=0.42,
    hand_length=0.26,
    upper_arm_radius=0.095,
    elbow_radius=0.080,
    wrist_radius=0.062,
    hand_radius=0.155,
    thigh_radius=0.150,
    knee_radius=0.115,
    ankle_radius=0.090,
    hair=Hair(length=0.0, volume=1.0, tail_length=0.0),
)

PRESETS: dict[str, Proportions] = {
    p.name: p for p in (HEROIC_6H, REALISTIC_75H, CHIBI_4H, TRICKSTER_3H)
}


def preset(name: str) -> Proportions:
    if name not in PRESETS:
        raise ProportionError(
            f"unknown preset {name!r}; available: {sorted(PRESETS)}"
        )
    chosen = PRESETS[name]
    chosen.validate()
    return chosen
