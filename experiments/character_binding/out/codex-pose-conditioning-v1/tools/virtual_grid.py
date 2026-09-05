#!/usr/bin/env python3
"""Recover the virtual pixel size of a generated 'pixel art' render.

An image model cannot emit a 48x48 canvas, so it fakes one by painting NxN
blocks of flat colour on a large canvas. If that is what happened, every colour
edge lands on a multiple of N. This scores each candidate N by what fraction of
edges share one phase, which is cheap and gives a sharp peak at the true N.
"""
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png_tool import read_png

EDGE_DELTA = 30  # summed per-channel change that counts as a colour edge


def edges_1d(w, h, d, horizontal):
    """Positions of colour edges along one axis, pooled over sampled lines."""
    positions = Counter()
    outer = h if horizontal else w
    inner = w if horizontal else h
    step = max(1, outer // 120)
    for a in range(outer // 6, 5 * outer // 6, step):
        for b in range(1, inner):
            if horizontal:
                i = (a * w + b) * 4
                j = i - 4
            else:
                i = (b * w + a) * 4
                j = ((b - 1) * w + a) * 4
            if d[i + 3] <= 8 and d[j + 3] <= 8:
                continue
            if abs(d[i] - d[j]) + abs(d[i + 1] - d[j + 1]) + abs(d[i + 2] - d[j + 2]) > EDGE_DELTA:
                positions[b] += 1
    return positions


def phase_score(positions, block):
    """Best fraction of edges sharing one residue mod block."""
    total = sum(positions.values())
    if not total:
        return 0.0, 0
    buckets = Counter()
    for pos, count in positions.items():
        buckets[pos % block] += count
    phase, hits = buckets.most_common(1)[0]
    return hits / total, phase


def analyse(path):
    w, h, d = read_png(path)
    print(f"\n=== {path}")
    print(f"stored canvas   {w} x {h}")

    for axis, horizontal in (("x", True), ("y", False)):
        positions = edges_1d(w, h, d, horizontal)
        total = sum(positions.values())
        rows = []
        for block in range(2, 41):
            score, phase = phase_score(positions, block)
            # A random edge set scores about 1/block by chance; subtract that.
            rows.append((block, score, score - 1.0 / block, phase))
        rows.sort(key=lambda r: -r[2])
        print(f"\n  {axis}-axis edges sampled: {total}")
        print(f"  {'block':>5} {'in-phase':>9} {'over chance':>12} {'phase':>6} {'virtual':>9}")
        for block, score, lift, phase in rows[:6]:
            span = w if horizontal else h
            print(f"  {block:>5} {100 * score:>8.1f}% {100 * lift:>11.1f}% {phase:>6} {span // block:>9}")


if __name__ == "__main__":
    for path in sys.argv[1:]:
        analyse(path)
