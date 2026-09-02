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
import json
from dataclasses import asdict, dataclass
from pathlib import Path

from .measurements import Hair, Proportions
from .skeleton import HEAD, Ellipsoid

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
    def silhouette_head_height(self) -> int:
        """Height from the highest attachment to the neck."""
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


def row_runs(
    mask: bytes | bytearray, width: int, y: int
) -> tuple[tuple[int, int], ...]:
    """Contiguous covered runs in one row, inclusive at both ends."""
    runs: list[tuple[int, int]] = []
    start: int | None = None
    for x in range(width):
        covered = bool(mask[y * width + x])
        if covered and start is None:
            start = x
        if not covered and start is not None:
            runs.append((start, x - 1))
            start = None
    if start is not None:
        runs.append((start, width - 1))
    return tuple(runs)


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
class HeadAttachment:
    """One image-derived head silhouette lobe, in head units."""

    name: str
    center_x: float
    center_y: float
    radius_x: float
    radius_y: float
    radius_z: float


@dataclass(frozen=True)
class Constraints:
    """Reusable geometry extracted from one freely generated reference."""

    proportions: Proportions
    head_attachments: tuple[HeadAttachment, ...]
    top_overhang: float
    source_size: tuple[int, int]
    subject_bounds: tuple[int, int]
    center_x_px: float

    @property
    def frame_height_hu(self) -> float:
        return self.proportions.heads_tall + self.top_overhang


def attachment_volumes(
    attachments: tuple[HeadAttachment, ...],
) -> tuple[Ellipsoid, ...]:
    """Build head-parented volumes that survive every later pose."""
    return tuple(
        Ellipsoid(
            item.name,
            "head",
            (item.center_x, item.center_y, 0.0),
            item.radius_x,
            item.radius_y,
            item.radius_z,
            HEAD,
        )
        for item in attachments
    )


@dataclass(frozen=True)
class SilhouetteScore:
    intersection: int
    union: int
    reference_only: int
    fitted_only: int

    @property
    def iou(self) -> float:
        return self.intersection / self.union if self.union else 1.0


def compare_silhouettes(
    reference: bytes | bytearray,
    fitted: bytes | bytearray,
    width: int,
    first_row: int,
    last_row: int,
) -> SilhouetteScore:
    """Compare registered masks over an inclusive vertical band."""
    if len(reference) != len(fitted) or len(reference) != width * (len(reference) // width):
        raise FitError("silhouette buffers do not share one rectangular size")
    if not 0 <= first_row <= last_row < len(reference) // width:
        raise FitError(f"invalid silhouette rows {first_row}..{last_row}")

    intersection = union = reference_only = fitted_only = 0
    for y in range(first_row, last_row + 1):
        for x in range(width):
            index = y * width + x
            in_reference = bool(reference[index])
            in_fitted = bool(fitted[index])
            intersection += in_reference and in_fitted
            union += in_reference or in_fitted
            reference_only += in_reference and not in_fitted
            fitted_only += in_fitted and not in_reference
    return SilhouetteScore(intersection, union, reference_only, fitted_only)


def write_constraints(path: Path, constraints: Constraints) -> None:
    payload = {
        "version": 1,
        "proportions": asdict(constraints.proportions),
        "head_attachments": [asdict(item) for item in constraints.head_attachments],
        "framing": {
            "top_overhang": constraints.top_overhang,
            "source_size": constraints.source_size,
            "subject_bounds": constraints.subject_bounds,
            "center_x_px": constraints.center_x_px,
        },
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def load_constraints(path: Path) -> Constraints:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if payload["version"] != 1:
            raise FitError(f"unsupported constraint version {payload['version']!r}")
        proportion_fields = dict(payload["proportions"])
        proportion_fields["hair"] = Hair(**proportion_fields["hair"])
        proportions = Proportions(**proportion_fields)
        proportions.validate()
        framing = payload["framing"]
        return Constraints(
            proportions=proportions,
            head_attachments=tuple(
                HeadAttachment(**item) for item in payload["head_attachments"]
            ),
            top_overhang=float(framing["top_overhang"]),
            source_size=tuple(int(value) for value in framing["source_size"]),
            subject_bounds=tuple(int(value) for value in framing["subject_bounds"]),
            center_x_px=float(framing["center_x_px"]),
        )
    except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        if isinstance(error, FitError):
            raise
        raise FitError(f"invalid constraint file {path}: {error}") from error


def _torso_center(spans: list[RowSpan | None], marks: Landmarks) -> float:
    centres = sorted(
        (span.left + span.right) / 2.0
        for span in spans[marks.shoulder : marks.crotch]
        if span is not None
    )
    if not centres:
        raise FitError("no torso rows between the shoulder and crotch landmarks")
    return centres[len(centres) // 2]


def _run_containing(
    runs: tuple[tuple[int, int], ...], x: float
) -> tuple[int, int] | None:
    return next((run for run in runs if run[0] <= x <= run[1]), None)


def _fit_head(
    mask: bytes | bytearray,
    width: int,
    marks: Landmarks,
    center_x: float,
) -> tuple[int, float, tuple[HeadAttachment, ...]]:
    center_column = min(width - 1, max(0, round(center_x)))
    skull_top = next(
        (
            y
            for y in range(marks.top, marks.neck)
            if mask[y * width + center_column]
        ),
        None,
    )
    if skull_top is None:
        raise FitError("could not find a central skull above the neck")

    head_px = marks.neck - skull_top
    if head_px <= 0:
        raise FitError("measured skull height is zero")

    width_start = skull_top + max(1, int(head_px * 0.45))
    central_widths = sorted(
        run[1] - run[0] + 1
        for y in range(width_start, marks.neck)
        if (run := _run_containing(row_runs(mask, width, y), center_x)) is not None
    )
    if not central_widths:
        raise FitError("could not measure the central skull width")
    skull_width_px = float(central_widths[len(central_widths) // 2])

    # A large paired feature above the central skull is not skull width. It is
    # persistent geometry: mouse ears, horns, hair buns, or a split headdress.
    # Fit each visible lobe independently so the guide retains the generated
    # silhouette instead of replacing the whole head with one enormous circle.
    lobe_rows: dict[str, list[tuple[int, int, int]]] = {"left": [], "right": []}
    scan_bottom = min(marks.neck, skull_top + max(2, head_px // 4))
    center_margin = skull_width_px * 0.15
    for y in range(marks.top, scan_bottom):
        for left, right in row_runs(mask, width, y):
            if right < center_x - center_margin:
                lobe_rows["left"].append((left, right, y))
            elif left > center_x + center_margin:
                lobe_rows["right"].append((left, right, y))

    attachments: list[HeadAttachment] = []
    for side in ("left", "right"):
        samples = lobe_rows[side]
        if not samples:
            continue
        left = min(sample[0] for sample in samples)
        right = max(sample[1] for sample in samples)
        top = min(sample[2] for sample in samples)
        radius_x_px = (right - left + 1) / 2.0
        if radius_x_px < head_px * 0.12:
            continue
        # Only the free upper arc is separable from the skull. The attachment's
        # hidden lower arc is inferred as near-round and checked in the overlay.
        radius_y_px = radius_x_px * 1.08
        lobe_center_x = (left + right) / 2.0
        lobe_center_y = top + radius_y_px
        attachments.append(
            HeadAttachment(
                name=f"head_lobe_{side}",
                center_x=(lobe_center_x - center_x) / head_px,
                center_y=(marks.neck - lobe_center_y) / head_px,
                radius_x=radius_x_px / head_px,
                radius_y=radius_y_px / head_px,
                radius_z=radius_x_px / head_px * 0.45,
            )
        )

    return skull_top, skull_width_px, tuple(attachments)


@dataclass(frozen=True)
class Fit:
    proportions: Proportions
    landmarks: Landmarks
    skull_top: int
    center_x_px: float
    head_attachments: tuple[HeadAttachment, ...]
    measured: tuple[str, ...]
    carried: tuple[str, ...]
    source_size: tuple[int, int]
    warning: str | None = None

    @property
    def head_height(self) -> int:
        return self.landmarks.neck - self.skull_top

    def constraints(self) -> Constraints:
        return Constraints(
            proportions=self.proportions,
            head_attachments=self.head_attachments,
            top_overhang=(self.skull_top - self.landmarks.top) / self.head_height,
            source_size=self.source_size,
            subject_bounds=(self.landmarks.top, self.landmarks.bottom),
            center_x_px=self.center_x_px,
        )

    def report(self) -> str:
        p = self.proportions
        lines = [
            f"fitted '{p.name}' from a {self.landmarks.bottom - self.landmarks.top + 1}px figure",
            f"  skull height  {self.head_height}px",
            f"  top overhang  {self.skull_top - self.landmarks.top}px",
            f"  heads tall    {p.heads_tall:.2f}",
            f"  head width    {p.head_width:.2f} HU",
            f"  head lobes    {len(self.head_attachments)}",
            f"  shoulders     {p.shoulder_width:.2f} HU",
            f"  waist / hip   {p.waist_width:.2f} / {p.hip_width:.2f} HU",
            f"  inseam        {p.inseam:.2f} HU",
            "",
            f"measured from the image: {', '.join(self.measured)}",
            f"carried from the base preset (not visible in a front view): "
            f"{', '.join(self.carried)}",
        ]
        for item in self.head_attachments:
            lines.append(
                f"  {item.name}: center ({item.center_x:.2f}, {item.center_y:.2f}) HU, "
                f"radii {item.radius_x:.2f} x {item.radius_y:.2f} HU"
            )
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
    """Fit reusable geometry to an isolated, front-facing standing figure.

    Paired lobes above the central skull are retained as separate head-parented
    attachments. The overlay remains the authority for ambiguous silhouettes.
    """
    spans = row_spans(mask, width, height)
    marks = find_landmarks(spans)
    center_x = _torso_center(spans, marks)
    skull_top, skull_width_px, head_attachments = _fit_head(
        mask, width, marks, center_x
    )
    head_px = float(marks.neck - skull_top)
    if head_px <= 0.0:
        raise FitError("measured head height is zero")

    def hu(pixels: float) -> float:
        return pixels / head_px

    torso_px = marks.crotch - marks.shoulder
    head_width = hu(skull_width_px)
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
        heads_tall=hu(marks.bottom - skull_top),
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
        skull_top=skull_top,
        center_x_px=center_x,
        head_attachments=head_attachments,
        measured=(
            "heads_tall",
            "head_width",
            "head silhouette lobes",
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
        source_size=(width, height),
        warning=shadow_warning(spans, marks),
    )
