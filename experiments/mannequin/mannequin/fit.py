"""Derive a measurement set from a generated character image.

This inverts the usual direction. Instead of authoring proportions by hand and
hoping a generator matches them, the character is generated freely — which is
where the style comes from, and generators are good at it — and the rig is then
fitted to the result. A guide shaped like the character poses it; a guide shaped
like a stranger stretches it.

Only a front-facing standing figure can be fitted. Two landmarks do the work,
and both are things a silhouette genuinely shows:

* the **neck**, the narrowest row in the upper part of the figure, which
  separates head from body;
* the **crotch**, the first row where the body's coverage splits into two runs,
  which separates torso from legs.

Vertical closure then holds by construction: head, neck, torso and inseam are
consecutive slices of the same measured height, so they sum to it exactly and
`Proportions.validate` cannot fail on a fitted figure.

Depths and limb radii are not visible in a front view. Those are carried from a
base preset, scaled so their relationship to the measured widths is preserved,
and reported separately so nobody mistakes them for measurements.
"""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass

from .measurements import Proportions

# A row must be this much wider than the neck to count as the shoulder line.
SHOULDER_WIDTH_RATIO = 1.15

# Fraction of torso height above the waist. The waist is not reliably visible on
# a stylised character, so the torso is split at a fixed ratio rather than
# guessed at from a bump in the profile.
WAIST_SPLIT = 0.62

# Arm reach as a multiple of torso height. A standing figure's hands sit at
# about the hip line, so the arm spans the torso from shoulder to crotch.
ARM_REACH_OF_TORSO = 1.0


class FitError(ValueError):
    """Raised when an image cannot be fitted."""


@dataclass(frozen=True)
class RowSpan:
    left: int
    right: int
    runs: int

    @property
    def width(self) -> int:
        return self.right - self.left + 1


@dataclass(frozen=True)
class Landmarks:
    top: int
    neck: int
    shoulder: int
    crotch: int
    bottom: int

    @property
    def head_height(self) -> int:
        return self.neck - self.top


def row_spans(mask: bytes | bytearray, width: int, height: int) -> list[RowSpan | None]:
    """Leftmost and rightmost covered pixel per row, plus how many runs it has.

    The run count is what finds the crotch: a standing figure is one run through
    the torso and two once the legs separate.
    """
    spans: list[RowSpan | None] = []
    for y in range(height):
        base = y * width
        left, right, runs, inside = width, -1, 0, False
        for x in range(width):
            covered = bool(mask[base + x])
            if covered:
                if not inside:
                    runs += 1
                    inside = True
                if x < left:
                    left = x
                right = x
            else:
                inside = False
        spans.append(RowSpan(left, right, runs) if right >= 0 else None)
    return spans


def find_landmarks(spans: list[RowSpan | None]) -> Landmarks:
    occupied = [span for span in spans if span is not None]
    if not occupied:
        raise FitError("image has no subject to fit")

    # The figure's extent ignores hairline rows. Isolation on a generated frame
    # leaves slivers — antialiased edges, a stray column the background flood
    # could not reach around — and a two-pixel-wide sliver reaching the canvas
    # edge dragged one fit's measured bottom down by 113px, throwing off the
    # height and every measurement derived from it. A character's foot is not
    # two pixels wide.
    noise_floor = max(3, int(max(span.width for span in occupied) * 0.02))
    filled = [
        y
        for y, span in enumerate(spans)
        if span is not None and span.width >= noise_floor
    ]
    if not filled:
        raise FitError(
            f"no row is at least {noise_floor}px wide; the subject is all slivers"
        )
    top, bottom = filled[0], filled[-1]
    figure_height = bottom - top + 1
    if figure_height < 32:
        raise FitError(f"subject is only {figure_height}px tall; too small to fit")

    # The neck is the narrowest row in the upper *half*, skipping the very top
    # where a rounded crown tapers to nothing on its own.
    #
    # The window has to reach past the midpoint. A big-eared or big-hatted
    # character is widest at the ears and narrower through the body, so the
    # classical "neck is a dip between the head and the shoulders" does not
    # hold — there is only one dip, at the chin, and on a two-and-a-half-head
    # character that dip sits close to 45% of the way down. Searching only the
    # upper third found the crease between ear and cheek instead and measured a
    # head less than half its true height.
    search_top = top + max(1, figure_height // 12)
    search_bottom = top + max(search_top - top + 2, int(figure_height * 0.55))
    candidates = [
        (spans[y].width, y)
        for y in range(search_top, min(search_bottom, bottom))
        if spans[y] is not None
    ]
    if not candidates:
        raise FitError("could not find a neck; is the figure front-facing?")
    neck = min(candidates)[1]
    neck_width = spans[neck].width

    # The shoulder is the first row below the neck that is decisively wider.
    shoulder = neck
    for y in range(neck + 1, bottom + 1):
        span = spans[y]
        if span is not None and span.width >= neck_width * SHOULDER_WIDTH_RATIO:
            shoulder = y
            break
    if shoulder == neck:
        shoulder = neck + max(1, figure_height // 40)

    # The crotch is where the legs separate. Search only the lower half so an
    # arm held clear of the body cannot be mistaken for it.
    crotch = None
    for y in range(top + figure_height // 2, bottom + 1):
        span = spans[y]
        if span is not None and span.runs >= 2:
            crotch = y
            break
    if crotch is None:
        # Legs that touch all the way down: fall back to the classical midpoint
        # of the figure, which is close enough to seed a fit the user can nudge.
        crotch = top + int(figure_height * 0.55)

    if not top < neck < shoulder < crotch < bottom:
        raise FitError(
            f"landmarks are out of order (top {top}, neck {neck}, "
            f"shoulder {shoulder}, crotch {crotch}, bottom {bottom})"
        )
    return Landmarks(top=top, neck=neck, shoulder=shoulder, crotch=crotch, bottom=bottom)


def _median_width(spans: list[RowSpan | None], first: int, last: int) -> float:
    widths = sorted(s.width for s in spans[first:last] if s is not None)
    if not widths:
        raise FitError(f"no covered rows between {first} and {last}")
    return float(widths[len(widths) // 2])


def _max_width(spans: list[RowSpan | None], first: int, last: int) -> float:
    widths = [s.width for s in spans[first:last] if s is not None]
    if not widths:
        raise FitError(f"no covered rows between {first} and {last}")
    return float(max(widths))


def shadow_warning(spans: list[RowSpan | None], marks: Landmarks) -> str | None:
    """Warn when the bottom of the figure looks like a drop shadow, not feet.

    A generated character usually sits on an elliptical shadow. It is not the
    background colour, so isolation keeps it, and it then counts as part of the
    figure: on the first mouse fitted here it added 13% to the measured height
    and dragged the crotch and inseam down with it. Feet are two narrow runs;
    a shadow is one wide one, so the two are easy to tell apart.
    """
    figure_height = marks.bottom - marks.top + 1
    foot_band = marks.bottom - max(2, figure_height // 12)
    lower = [s for s in spans[foot_band : marks.bottom + 1] if s is not None]
    legs = [s for s in spans[marks.crotch : foot_band] if s is not None]
    if not lower or not legs:
        return None

    widest_lower = max(s.width for s in lower)
    typical_leg = sorted(s.width for s in legs)[len(legs) // 2]
    single_run = all(s.runs == 1 for s in lower)
    if single_run and widest_lower > typical_leg * 1.4:
        return (
            f"the bottom {len(lower)}px is one run {widest_lower}px wide against "
            f"{typical_leg}px of leg above it, which looks like a drop shadow "
            "rather than feet. It inflates the height and every measurement "
            "derived from it. Regenerate the reference with 'drop shadow' in "
            "the negative prompt, or crop it off."
        )
    return None


@dataclass(frozen=True)
class Fit:
    proportions: Proportions
    landmarks: Landmarks
    measured: tuple[str, ...]
    carried: tuple[str, ...]
    warning: str | None = None

    def report(self) -> str:
        p = self.proportions
        lines = [
            f"fitted '{p.name}' from a {self.landmarks.bottom - self.landmarks.top + 1}px figure",
            f"  head height   {self.landmarks.head_height}px",
            f"  heads tall    {p.heads_tall:.2f}",
            f"  head width    {p.head_width:.2f} HU",
            f"  shoulders     {p.shoulder_width:.2f} HU",
            f"  waist / hip   {p.waist_width:.2f} / {p.hip_width:.2f} HU",
            f"  inseam        {p.inseam:.2f} HU",
            "",
            f"measured from the image: {', '.join(self.measured)}",
            f"carried from the base preset (not visible in a front view): "
            f"{', '.join(self.carried)}",
        ]
        if self.warning is not None:
            lines += ["", f"WARNING: {self.warning}"]
        return "\n".join(lines)


def fit_proportions(
    mask: bytes | bytearray,
    width: int,
    height: int,
    base: Proportions,
    name: str = "fitted",
) -> Fit:
    """Fit a measurement set to an isolated, front-facing standing figure.

    Ears, hats and hair inflate the head measurements, because a silhouette
    cannot tell them from a skull. Check the overlay before trusting the result.
    """
    spans = row_spans(mask, width, height)
    marks = find_landmarks(spans)
    head_px = float(marks.head_height)
    if head_px <= 0.0:
        raise FitError("measured head height is zero")

    def hu(pixels: float) -> float:
        return pixels / head_px

    torso_px = marks.crotch - marks.shoulder
    # Median rather than max across the head band: on a character with ears or a
    # hat the widest row is the ears, and the skull is what the rig needs.
    head_width = hu(_median_width(spans, marks.top, marks.neck))
    shoulder_width = hu(
        _max_width(spans, marks.shoulder, marks.shoulder + max(1, torso_px // 3))
    )
    waist_width = hu(
        _median_width(spans, marks.shoulder + torso_px // 3, marks.crotch)
    )
    hip_width = hu(_max_width(spans, max(marks.shoulder, marks.crotch - torso_px // 4),
                              marks.crotch))

    # Depth is invisible here, so keep each depth in the same relation to its own
    # width that the base preset uses.
    def depth_like(width_value: float, base_width: float, base_depth: float) -> float:
        return width_value * (base_depth / base_width)

    torso_hu = hu(torso_px)

    # Arms are not directly measurable in a front view — they hang against the
    # body and merge with it. But their length is implied: on a standing figure
    # the hands reach roughly the hip line, so the whole arm spans the torso.
    # Carrying the base preset's arms instead left this mouse with hands down at
    # its feet, which is the most visible error in an overlay.
    base_reach = base.upper_arm + base.forearm + base.hand_length
    if base_reach <= 0.0:
        raise FitError("base preset has no arm length to scale")
    reach_scale = (torso_hu * ARM_REACH_OF_TORSO) / base_reach

    fitted = dataclasses.replace(
        base,
        name=name,
        upper_arm=base.upper_arm * reach_scale,
        forearm=base.forearm * reach_scale,
        hand_length=base.hand_length * reach_scale,
        heads_tall=hu(marks.bottom - marks.top + 1),
        head_width=head_width,
        head_depth=depth_like(head_width, base.head_width, base.head_depth),
        neck_length=hu(marks.shoulder - marks.neck),
        neck_width=head_width * (base.neck_width / base.head_width),
        shoulder_width=shoulder_width,
        chest_width=(shoulder_width + waist_width) / 2.0,
        chest_depth=depth_like(shoulder_width, base.shoulder_width, base.chest_depth),
        waist_width=waist_width,
        waist_depth=depth_like(waist_width, base.waist_width, base.waist_depth),
        hip_width=hip_width,
        hip_depth=depth_like(hip_width, base.hip_width, base.hip_depth),
        shoulder_to_waist=torso_hu * WAIST_SPLIT,
        waist_to_hip=torso_hu * (1.0 - WAIST_SPLIT),
        inseam=hu(marks.bottom - marks.crotch),
    )
    fitted.validate()
    return Fit(
        proportions=fitted,
        landmarks=marks,
        measured=(
            "heads_tall",
            "head_width",
            "neck_length",
            "shoulder_width",
            "waist_width",
            "hip_width",
            "shoulder_to_waist",
            "waist_to_hip",
            "inseam",
            "arm length (from torso height)",
        ),
        carried=(
            "all depths",
            "limb radii",
            "foot_length",
            "eye_radius",
        ),
        warning=shadow_warning(spans, marks),
    )
