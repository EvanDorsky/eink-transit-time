#!/usr/bin/env python3
"""Convert a binary PGM (P5) to PNG using only the stdlib."""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = f.read()
    # header: P5 <w> <h> <maxval> then raw bytes
    parts = data.split(maxsplit=4)
    assert parts[0] == b"P5", "not a binary PGM"
    w, h = int(parts[1]), int(parts[2])
    pixels = parts[4][: w * h]

    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)  # 8-bit grayscale
    raw = b"".join(b"\x00" + pixels[y * w : (y + 1) * w] for y in range(h))
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    with open(dst, "wb") as f:
        f.write(png)
    print(f"wrote {dst}")


if __name__ == "__main__":
    main()
