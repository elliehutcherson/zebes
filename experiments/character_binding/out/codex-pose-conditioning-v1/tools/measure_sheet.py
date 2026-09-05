#!/usr/bin/env python3
"""Pre-registered registration metrics for a generated run sheet.

Reports the same five numbers for every attempt so runs are comparable:
figure count, height spread, baseline spread, distinct colours, pixel-block size.
"""
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png_tool import read_png

# A generated sheet's background is near-white rather than transparent, so
# "ink" means opaque and darker than this on any channel.
WHITE = 235


def measure(path, expected_figures=5):
    w, h, d = read_png(path)

    def ink(x, y):
        i = (y * w + x) * 4
        r, g, b, a = d[i], d[i + 1], d[i + 2], d[i + 3]
        return a > 8 and (r < WHITE or g < WHITE or b < WHITE)

    columns = [any(ink(x, y) for y in range(0, h, 2)) for x in range(w)]
    runs, start = [], None
    for x, filled in enumerate(columns + [False]):
        if filled and start is None:
            start = x
        if not filled and start is not None:
            if x - start > 20:
                runs.append((start, x - 1))
            start = None

    boxes = []
    for x0, x1 in runs:
        ys = [y for y in range(h) if any(ink(x, y) for x in range(x0, x1 + 1))]
        boxes.append((x0, min(ys), x1, max(ys)))

    colours = set()
    for i in range(0, len(d), 4):
        if d[i + 3] > 8:
            colours.add(bytes(d[i:i + 3]))

    # A true pixel-art grid repeats each logical pixel N times horizontally.
    lengths = Counter()
    for row in range(h // 4, 3 * h // 4, 7):
        run = 1
        for x in range(1, w):
            here = bytes(d[(row * w + x) * 4:(row * w + x) * 4 + 3])
            prev = bytes(d[(row * w + x - 1) * 4:(row * w + x - 1) * 4 + 3])
            if here == prev:
                run += 1
            else:
                if run < 40:
                    lengths[run] += 1
                run = 1

    print(f"sheet            {w} x {h}")
    print(f"figures found    {len(boxes)} (expected {expected_figures})")
    print()
    print(f"{'fig':>3} {'x0':>6} {'x1':>6} {'width':>6} {'top':>6} {'bottom':>6} {'height':>7}")
    for i, (x0, y0, x1, y1) in enumerate(boxes, 1):
        print(f"{i:>3} {x0:>6} {x1:>6} {x1 - x0 + 1:>6} {y0:>6} {y1:>6} {y1 - y0 + 1:>7}")

    if boxes:
        heights = [b[3] - b[1] + 1 for b in boxes]
        bottoms = [b[3] for b in boxes]
        spread = max(heights) - min(heights)
        print()
        print(f"height spread    {spread} px = {100 * spread / min(heights):.1f}%")
        print(f"baseline spread  {max(bottoms) - min(bottoms)} px")
        gaps = [boxes[i + 1][0] - boxes[i][2] for i in range(len(boxes) - 1)]
        print(f"cell gaps        {gaps}")
    print(f"distinct colours {len(colours)}")
    print(f"run lengths      {lengths.most_common(6)}")


if __name__ == "__main__":
    measure(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 5)
