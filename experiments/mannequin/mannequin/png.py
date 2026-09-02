"""Minimal PNG writer.

Pillow is not a dependency of this experiment, and the only thing needed is
non-interlaced 8-bit RGBA with a single IDAT. `zlib` and `struct` cover that,
so the tool runs on a bare interpreter.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def write_rgba(path: Path, width: int, height: int, pixels: bytearray) -> None:
    """Write RGBA8 pixel data, four bytes per pixel in row-major order."""
    expected = width * height * 4
    if len(pixels) != expected:
        raise ValueError(
            f"pixel buffer is {len(pixels)} bytes, expected {expected} "
            f"for {width}x{height} RGBA"
        )

    stride = width * 4
    raw = bytearray()
    for y in range(height):
        # Filter type 0 (None). The images are flat colour regions, so a
        # smarter filter buys compression that nothing here needs.
        raw.append(0)
        raw += pixels[y * stride : (y + 1) * stride]

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + _chunk(b"IEND", b"")
    )


_CHANNELS_BY_COLOR_TYPE = {0: 1, 2: 3, 4: 2, 6: 4}


def read_rgba(path: Path) -> tuple[int, int, bytearray]:
    """Decode a non-interlaced 8-bit PNG to RGBA8.

    Deliberately narrow. It exists so the drift gate can measure a frame
    produced elsewhere, and a silently mis-decoded frame would corrupt the very
    measurement it is for — so palette, 16-bit, and interlaced files raise
    instead of being approximated.
    """
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")

    offset = 8
    header: tuple[int, ...] | None = None
    compressed = bytearray()
    while offset + 8 <= len(data):
        (length,) = struct.unpack(">I", data[offset : offset + 4])
        tag = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif tag == b"IDAT":
            compressed += payload
        elif tag == b"IEND":
            break

    if header is None:
        raise ValueError(f"{path} has no IHDR chunk")

    width, height, bit_depth, color_type, compression, filter_method, interlace = header
    if bit_depth != 8:
        raise ValueError(f"{path}: only 8-bit PNGs are supported, got {bit_depth}-bit")
    if interlace != 0:
        raise ValueError(f"{path}: interlaced PNGs are not supported")
    if color_type not in _CHANNELS_BY_COLOR_TYPE:
        raise ValueError(
            f"{path}: unsupported colour type {color_type} "
            "(palette images are not supported)"
        )
    if compression != 0 or filter_method != 0:
        raise ValueError(f"{path}: unsupported compression or filter method")

    channels = _CHANNELS_BY_COLOR_TYPE[color_type]
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    if len(raw) != (stride + 1) * height:
        raise ValueError(
            f"{path}: decompressed to {len(raw)} bytes, expected "
            f"{(stride + 1) * height} for {width}x{height}"
        )

    decoded = bytearray(stride * height)
    previous = bytearray(stride)
    pos = 0
    for y in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos : pos + stride])
        pos += stride
        _undo_filter(filter_type, line, previous, channels)
        decoded[y * stride : (y + 1) * stride] = line
        previous = line

    return width, height, _to_rgba(decoded, width * height, color_type)


def _undo_filter(
    filter_type: int, line: bytearray, previous: bytearray, channels: int
) -> None:
    if filter_type == 0:
        return
    if filter_type not in (1, 2, 3, 4):
        raise ValueError(f"unknown PNG filter type {filter_type}")

    for i in range(len(line)):
        a = line[i - channels] if i >= channels else 0
        b = previous[i]
        if filter_type == 1:
            line[i] = (line[i] + a) & 0xFF
        elif filter_type == 2:
            line[i] = (line[i] + b) & 0xFF
        elif filter_type == 3:
            line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
        else:
            c = previous[i - channels] if i >= channels else 0
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            nearest = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            line[i] = (line[i] + nearest) & 0xFF


def _to_rgba(data: bytearray, count: int, color_type: int) -> bytearray:
    if color_type == 6:
        return data
    out = bytearray(count * 4)
    for i in range(count):
        if color_type == 0:
            value = data[i]
            out[i * 4 : i * 4 + 4] = bytes((value, value, value, 255))
        elif color_type == 2:
            out[i * 4 : i * 4 + 3] = data[i * 3 : i * 3 + 3]
            out[i * 4 + 3] = 255
        else:
            value, alpha = data[i * 2], data[i * 2 + 1]
            out[i * 4 : i * 4 + 4] = bytes((value, value, value, alpha))
    return out
