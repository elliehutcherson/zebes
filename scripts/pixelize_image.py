#!/usr/bin/env python3
"""Convert artwork into an exact, integer-scaled pixel-art image."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

from PIL import Image, UnidentifiedImageError


def center_crop_to_aspect(image: Image.Image, width: int, height: int) -> Image.Image:
    if width <= 0 or height <= 0:
        raise ValueError("Logical dimensions must be positive integers.")

    divisor = math.gcd(width, height)
    aspect_width = width // divisor
    aspect_height = height // divisor
    multiplier = min(image.width // aspect_width, image.height // aspect_height)
    if multiplier == 0:
        raise ValueError("Input image is too small for the requested aspect ratio.")

    crop_width = aspect_width * multiplier
    crop_height = aspect_height * multiplier
    left = (image.width - crop_width) // 2
    top = (image.height - crop_height) // 2
    return image.crop((left, top, left + crop_width, top + crop_height))


def pixelize(image: Image.Image, width: int, height: int, scale: int) -> Image.Image:
    if scale <= 0:
        raise ValueError("Scale must be a positive integer.")

    cropped = center_crop_to_aspect(image, width, height)
    logical = cropped.resize((width, height), Image.Resampling.BOX)
    return logical.resize((width * scale, height * scale), Image.Resampling.NEAREST)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Center-crop artwork and convert it to integer-scaled pixel art."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--logical-width", type=int, default=320)
    parser.add_argument("--logical-height", type=int, default=180)
    parser.add_argument("--scale", type=int, default=4)
    return parser.parse_args()


def run(args: argparse.Namespace) -> None:
    if not args.input.is_file():
        raise ValueError(f"Image does not exist: {args.input}")

    with Image.open(args.input) as source:
        output = pixelize(
            source.convert("RGBA"), args.logical_width, args.logical_height, args.scale
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    output.save(args.output)
    print(
        f"Wrote {args.output} ({output.width}x{output.height}) from "
        f"{args.logical_width}x{args.logical_height} logical pixels."
    )


def main() -> int:
    try:
        run(parse_args())
    except (OSError, UnidentifiedImageError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
