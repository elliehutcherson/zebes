"""Subject isolation for generated frames.

The drift gate refuses opaque images, because measuring an un-isolated frame
compares the background's bounding box rather than the character's. Generated
frames arrive opaque, so something has to separate them first.

Two things had to be got right, and the first attempt got both wrong.

A flat threshold eats the silhouette: the character is drawn with thick dark
outlines and shadowed recesses, so any cut dark enough to catch the background
also punches holes through the figure. The background is therefore found by
flooding inward from the border, which never reaches enclosed dark pixels.

And the test must be colour distance from the actual background, not luminance.
A red cape has a luminance around 60 — below any ceiling low enough to catch a
dark background — so a luminance flood pours straight through the cape into the
figure's interior and shatters the mask into outlines. Saturated colour is far
from a desaturated background in RGB even when it is equally dark.

This is an experiment-side stand-in. The engine already owns a real matte in
`src/artwork/isolate_subject.h`; nothing here should graduate into production.
"""

from __future__ import annotations

from collections import deque
from pathlib import Path

from .png import read_rgba

# How far, in RGB Euclidean distance, a pixel may sit from the sampled corner
# colour and still count as background. Generous enough to swallow a vignette
# gradient, far short of the ~146 that separates a red cape from a dark
# desaturated ground.
DEFAULT_BACKGROUND_TOLERANCE = 48


class IsolationError(ValueError):
    """Raised when a frame cannot be separated from its background."""


def luminance(pixels: bytearray, count: int) -> list[int]:
    return [
        (pixels[i * 4] * 299 + pixels[i * 4 + 1] * 587 + pixels[i * 4 + 2] * 114)
        // 1000
        for i in range(count)
    ]


def background_color(pixels: bytearray, width: int, height: int) -> tuple[int, int, int]:
    """The frame's background colour: the per-channel median of its border ring.

    The whole ring rather than the four corners. A subject that runs off one
    edge occupies two corners, and a four-sample median then averages background
    against character and returns a colour that is neither — which makes every
    subsequent distance test meaningless.
    """
    border = [y * width for y in range(height)]
    border += [y * width + width - 1 for y in range(height)]
    border += [x for x in range(1, width - 1)]
    border += [(height - 1) * width + x for x in range(1, width - 1)]

    channels = []
    for c in range(3):
        values = sorted(pixels[i * 4 + c] for i in border)
        channels.append(values[len(values) // 2])
    return tuple(channels)  # type: ignore[return-value]


def subject_mask(
    pixels: bytearray,
    width: int,
    height: int,
    tolerance: int = DEFAULT_BACKGROUND_TOLERANCE,
) -> bytearray:
    """Coverage mask of the subject, found by flooding background from the edge.

    Every border pixel must itself be background; a figure touching the frame
    edge means the render was cropped, and its measurements would be wrong in a
    way no later step could detect.
    """
    reference = background_color(pixels, width, height)
    limit = tolerance * tolerance
    background = bytearray(width * height)
    queue: deque[int] = deque()

    def is_background(index: int) -> bool:
        base = index * 4
        total = 0
        for c in range(3):
            delta = pixels[base + c] - reference[c]
            total += delta * delta
        return total <= limit

    def consider(index: int) -> None:
        if not background[index] and is_background(index):
            background[index] = 1
            queue.append(index)

    for x in range(width):
        consider(x)
        consider((height - 1) * width + x)
    for y in range(height):
        consider(y * width)
        consider(y * width + width - 1)

    if not queue:
        raise IsolationError(
            f"no border pixel is within {tolerance} of the sampled background "
            f"colour {reference}; this frame has no separable background"
        )

    while queue:
        index = queue.popleft()
        x, y = index % width, index // width
        if x > 0:
            consider(index - 1)
        if x < width - 1:
            consider(index + 1)
        if y > 0:
            consider(index - width)
        if y < height - 1:
            consider(index + width)

    mask = bytearray(1 if not b else 0 for b in background)
    covered = sum(mask)
    if covered == 0:
        raise IsolationError("isolation found no subject; the frame is all background")
    if covered == width * height:
        raise IsolationError("isolation found no background; the frame is all subject")
    return mask


def subject_mask_from_png(
    path: Path, tolerance: int = DEFAULT_BACKGROUND_TOLERANCE
) -> tuple[bytearray, int, int]:
    width, height, pixels = read_rgba(path)
    return subject_mask(pixels, width, height, tolerance), width, height


def touches_border(mask: bytearray, width: int, height: int) -> bool:
    """Whether the subject reaches a canvas edge, meaning it may be cropped."""
    if any(mask[x] or mask[(height - 1) * width + x] for x in range(width)):
        return True
    return any(mask[y * width] or mask[y * width + width - 1] for y in range(height))
