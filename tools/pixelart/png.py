"""Minimal PNG writer for numpy RGBA arrays (no PIL needed)."""
import struct
import zlib

import numpy as np


def write_png(path, rgba):
    rgba = np.ascontiguousarray(rgba, dtype=np.uint8)
    h, w, _ = rgba.shape
    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def upscale(rgba, k):
    """Nearest-neighbour upscale (for previews)."""
    return np.repeat(np.repeat(rgba, k, axis=0), k, axis=1)
