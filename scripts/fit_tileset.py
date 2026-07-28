#!/usr/bin/env python3
"""Fit generated artwork into the exact geometry of a tile template."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, UnidentifiedImageError
from scipy.ndimage import distance_transform_edt
from scipy.optimize import linear_sum_assignment


@dataclass(frozen=True)
class Component:
    label: int
    x: int
    y: int
    width: int
    height: int
    area: int
    center_x: float
    center_y: float


def load_rgba(path: Path) -> np.ndarray:
    if not path.is_file():
        raise ValueError(f"Image does not exist: {path}")
    with Image.open(path) as image:
        return np.asarray(image.convert("RGBA"))


def foreground_mask(
    rgba: np.ndarray, alpha_threshold: int, background_threshold: float
) -> np.ndarray:
    """Prefer meaningful alpha; otherwise remove the median border color."""
    alpha = rgba[:, :, 3]
    transparent_fraction = float(np.mean(alpha <= alpha_threshold))
    if transparent_fraction >= 0.01:
        return alpha > alpha_threshold

    rgb = rgba[:, :, :3].astype(np.float32)
    border = np.concatenate((rgb[0], rgb[-1], rgb[:, 0], rgb[:, -1]))
    background = np.median(border, axis=0)
    distance = np.linalg.norm(rgb - background, axis=2)
    return distance > background_threshold


def find_components(mask: np.ndarray, min_area: int) -> tuple[list[Component], np.ndarray]:
    count, labels, stats, centroids = cv2.connectedComponentsWithStats(
        mask.astype(np.uint8), connectivity=8
    )
    components = []
    for label in range(1, count):
        x, y, width, height, area = (int(value) for value in stats[label])
        if area < min_area:
            continue
        components.append(
            Component(
                label=label,
                x=x,
                y=y,
                width=width,
                height=height,
                area=area,
                center_x=float(centroids[label, 0]),
                center_y=float(centroids[label, 1]),
            )
        )
    components.sort(key=lambda component: (component.y, component.x))
    return components, labels


def select_generated_components(
    components: list[Component], expected_count: int
) -> list[Component]:
    if expected_count == 0:
        raise ValueError("The template contains no foreground components.")
    if len(components) < expected_count:
        raise ValueError(
            f"Found {len(components)} generated pieces; the template requires {expected_count}."
        )
    selected = sorted(components, key=lambda component: component.area, reverse=True)[
        :expected_count
    ]
    return sorted(selected, key=lambda component: (component.y, component.x))


def component_features(components: list[Component]) -> np.ndarray:
    left = min(component.x for component in components)
    top = min(component.y for component in components)
    right = max(component.x + component.width for component in components)
    bottom = max(component.y + component.height for component in components)
    width = max(1, right - left)
    height = max(1, bottom - top)
    total_area = max(1, sum(component.area for component in components))

    return np.asarray(
        [
            [
                (component.center_x - left) / width,
                (component.center_y - top) / height,
                math.log(component.width / component.height),
                math.log(component.area / total_area),
                math.log(component.width / width),
                math.log(component.height / height),
            ]
            for component in components
        ],
        dtype=np.float64,
    )


def auto_match(
    template_components: list[Component], generated_components: list[Component]
) -> tuple[dict[int, int], np.ndarray]:
    template_features = component_features(template_components)
    generated_features = component_features(generated_components)
    weights = np.asarray([5.0, 5.0, 1.5, 0.8, 1.0, 1.0])
    delta = (template_features[:, None, :] - generated_features[None, :, :]) * weights
    costs = np.sum(delta * delta, axis=2)
    template_indices, generated_indices = linear_sum_assignment(costs)
    return dict(zip(template_indices.tolist(), generated_indices.tolist())), costs


def validate_mapping(mapping: dict[int, int], expected_count: int) -> None:
    expected = set(range(expected_count))
    if set(mapping) != expected:
        raise ValueError("Mapping must contain every template component exactly once.")
    if set(mapping.values()) != expected:
        raise ValueError("Mapping must use every generated component exactly once.")


def load_mapping(path: Path, expected_count: int) -> dict[int, int]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        raw_mapping = data.get("template_to_generated", data)
        mapping = {int(key): int(value) for key, value in raw_mapping.items()}
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        raise ValueError(f"Invalid mapping file {path}: {error}") from error
    validate_mapping(mapping, expected_count)
    return mapping


def extend_rgb_to_bbox(rgb: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Fill pixels outside a component with its nearest foreground color."""
    if not np.any(mask):
        raise ValueError("A generated component has no usable pixels.")
    _, nearest = distance_transform_edt(~mask, return_indices=True)
    return rgb[nearest[0], nearest[1]]


def resize_rgb(rgb: np.ndarray, width: int, height: int) -> np.ndarray:
    shrinking = width <= rgb.shape[1] and height <= rgb.shape[0]
    interpolation = cv2.INTER_AREA if shrinking else cv2.INTER_CUBIC
    return cv2.resize(rgb, (width, height), interpolation=interpolation)


def fit_tileset(
    template_rgba: np.ndarray,
    generated_rgba: np.ndarray,
    template_components: list[Component],
    generated_components: list[Component],
    template_labels: np.ndarray,
    generated_labels: np.ndarray,
    mapping: dict[int, int],
    scale: int,
) -> np.ndarray:
    template_height, template_width = template_rgba.shape[:2]
    output_width = template_width * scale
    output_height = template_height * scale
    scaled_labels = cv2.resize(
        template_labels.astype(np.int32),
        (output_width, output_height),
        interpolation=cv2.INTER_NEAREST,
    )
    output = np.zeros((output_height, output_width, 4), dtype=np.uint8)

    for template_index, generated_index in mapping.items():
        target = template_components[template_index]
        source = generated_components[generated_index]
        source_slice = np.s_[
            source.y : source.y + source.height, source.x : source.x + source.width
        ]
        source_rgb = generated_rgba[source_slice][:, :, :3]
        source_foreground = generated_labels[source_slice] == source.label
        source_rgb = extend_rgb_to_bbox(source_rgb, source_foreground)

        target_mask = scaled_labels == target.label
        y_coordinates, x_coordinates = np.where(target_mask)
        if len(x_coordinates) == 0:
            raise ValueError(f"Scaled template component {template_index} is empty.")
        left, top = int(x_coordinates.min()), int(y_coordinates.min())
        right, bottom = int(x_coordinates.max()) + 1, int(y_coordinates.max()) + 1
        local_foreground = target_mask[top:bottom, left:right]
        region = output[top:bottom, left:right]
        fitted_rgb = resize_rgb(source_rgb, right - left, bottom - top)
        region[:, :, :3][local_foreground] = fitted_rgb[local_foreground]
        region[:, :, 3][local_foreground] = 255

    return output


def save_debug(
    path: Path,
    template_rgba: np.ndarray,
    generated_rgba: np.ndarray,
    template_components: list[Component],
    generated_components: list[Component],
    mapping: dict[int, int],
) -> None:
    inverse = {generated: template for template, generated in mapping.items()}
    generated_image = Image.fromarray(generated_rgba)
    generated_draw = ImageDraw.Draw(generated_image)
    for index, component in enumerate(generated_components):
        generated_draw.rectangle(
            (
                component.x,
                component.y,
                component.x + component.width - 1,
                component.y + component.height - 1,
            ),
            outline="red",
            width=3,
        )
        generated_draw.text(
            (component.x + 4, component.y + 4), f"G{index}->T{inverse[index]}", fill="red"
        )

    template_image = Image.fromarray(template_rgba)
    template_draw = ImageDraw.Draw(template_image)
    for index, component in enumerate(template_components):
        template_draw.rectangle(
            (
                component.x,
                component.y,
                component.x + component.width - 1,
                component.y + component.height - 1,
            ),
            outline="red",
            width=1,
        )
        template_draw.text(
            (component.x + 1, component.y + 1), f"T{index}->G{mapping[index]}", fill="red"
        )

    target_height = 900
    generated_image.thumbnail((700, target_height), Image.Resampling.LANCZOS)
    template_image.thumbnail((700, target_height), Image.Resampling.NEAREST)
    canvas = Image.new(
        "RGBA",
        (
            generated_image.width + template_image.width + 20,
            max(generated_image.height, template_image.height),
        ),
        (32, 32, 32, 255),
    )
    canvas.alpha_composite(generated_image)
    canvas.alpha_composite(template_image, (generated_image.width + 20, 0))
    canvas.save(path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fit generated artwork into an exact tile-template silhouette."
    )
    parser.add_argument("template", type=Path)
    parser.add_argument("generated", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--template-tile-size", type=int, default=16)
    parser.add_argument("--output-tile-size", type=int, default=32)
    parser.add_argument("--alpha-threshold", type=int, default=16)
    parser.add_argument("--background-threshold", type=float, default=24.0)
    parser.add_argument("--min-template-area", type=int, default=3)
    parser.add_argument("--min-generated-area", type=int, default=64)
    parser.add_argument("--max-match-cost", type=float, default=8.0)
    parser.add_argument("--mapping", type=Path, help="Optional manual component mapping JSON.")
    parser.add_argument("--report", type=Path, help="Write component and matching data as JSON.")
    parser.add_argument("--debug", type=Path, help="Write a component-match preview PNG.")
    return parser.parse_args()


def run(args: argparse.Namespace) -> None:
    if args.template_tile_size <= 0 or args.output_tile_size <= 0:
        raise ValueError("Tile sizes must be positive integers.")
    if args.output_tile_size % args.template_tile_size != 0:
        raise ValueError("Output tile size must be an integer multiple of template tile size.")
    if not 0 <= args.alpha_threshold <= 255:
        raise ValueError("Alpha threshold must be between 0 and 255.")
    if args.min_template_area <= 0 or args.min_generated_area <= 0:
        raise ValueError("Minimum component areas must be positive integers.")

    template_rgba = load_rgba(args.template)
    generated_rgba = load_rgba(args.generated)
    template_mask = foreground_mask(
        template_rgba, args.alpha_threshold, args.background_threshold
    )
    generated_mask = foreground_mask(
        generated_rgba, args.alpha_threshold, args.background_threshold
    )
    template_components, template_labels = find_components(
        template_mask, args.min_template_area
    )
    generated_candidates, generated_labels = find_components(
        generated_mask, args.min_generated_area
    )
    generated_components = select_generated_components(
        generated_candidates, len(template_components)
    )

    if args.mapping:
        mapping = load_mapping(args.mapping, len(template_components))
        costs = None
    else:
        mapping, costs = auto_match(template_components, generated_components)
        worst_cost = max(float(costs[key, value]) for key, value in mapping.items())
        if worst_cost > args.max_match_cost:
            raise ValueError(
                f"Automatic component match is uncertain (worst cost {worst_cost:.2f}, "
                f"limit {args.max_match_cost:.2f}). Review --debug output or supply --mapping."
            )

    scale = args.output_tile_size // args.template_tile_size
    output = fit_tileset(
        template_rgba,
        generated_rgba,
        template_components,
        generated_components,
        template_labels,
        generated_labels,
        mapping,
        scale,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(output).save(args.output)

    if args.debug:
        args.debug.parent.mkdir(parents=True, exist_ok=True)
        save_debug(
            args.debug,
            template_rgba,
            generated_rgba,
            template_components,
            generated_components,
            mapping,
        )

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        report = {
            "template_to_generated": {str(key): value for key, value in mapping.items()},
            "template_components": [asdict(component) for component in template_components],
            "generated_components": [asdict(component) for component in generated_components],
            "match_costs": (
                {str(key): float(costs[key, value]) for key, value in mapping.items()}
                if costs is not None
                else None
            ),
            "template_tile_size": args.template_tile_size,
            "output_tile_size": args.output_tile_size,
            "output_size": [int(output.shape[1]), int(output.shape[0])],
        }
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(
        f"Wrote {args.output} ({output.shape[1]}x{output.shape[0]}), "
        f"matched {len(template_components)} pieces at {args.output_tile_size}px tile density."
    )


def main() -> int:
    args = parse_args()
    try:
        run(args)
    except (OSError, UnidentifiedImageError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
