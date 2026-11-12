import argparse
from pathlib import Path
import struct


def texture(path, index):
    data = path.read_bytes()
    table, count = struct.unpack_from("<II", data, 12)
    if index >= count:
        raise ValueError(f"Missing texture {index} in {path}")
    start = table + struct.unpack_from("<I", data, table + index * 4)[0]
    mip, fmt, dimensions, flags = struct.unpack_from("<BBHI", data, start)
    width, height = 1 << (dimensions & 15), 1 << (dimensions >> 4)
    if fmt != 1 or mip >> 4 != 1 or width != 512 or height not in (32, 512):
        raise ValueError(f"Unexpected icon texture format in {path}")
    start += 8 + (12 if flags >> 24 == 0x10 else 0)
    pixels = data[start:start + width * height * 4]
    if len(pixels) != width * height * 4:
        raise ValueError(f"Truncated texture in {path}")
    return width, height, pixels


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def swizzle(data):
    pixels = bytearray(data)
    pixels[0::4], pixels[2::4] = data[2::4], data[0::4]
    return pixels


def encode_patch(width, height, original, replacement):
    difference = bytes(a ^ b for a, b in zip(original, replacement))
    changes = [i for i, value in enumerate(difference) if value]
    runs = []
    if changes:
        start = end = changes[0]
        for pos in changes[1:]:
            if pos - end > 16:
                runs.append((start, end + 1))
                start = pos
            end = pos
        runs.append((start, end + 1))
    patch = bytearray(struct.pack("<IIQQII", width, height, fingerprint(original),
                                  fingerprint(replacement), len(original), len(runs)))
    for start, end in runs:
        patch += struct.pack("<II", start, end - start) + difference[start:end]
    return patch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("original", type=Path)
    parser.add_argument("playstation", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    pack = bytearray(b"NGSICN02" + struct.pack("<II", 1, 4))
    for file, index in [(2771, 5), (2777, 30)]:
        name = f"{file:05}.GT1G0600"
        width, height, original = texture(args.original / name, index)
        ps_width, ps_height, ps = texture(args.playstation / name, index)
        if (width, height) != (ps_width, ps_height):
            raise ValueError(f"Icon texture dimensions differ in {name}")
        pack += encode_patch(width, height, original, ps)
        pack += encode_patch(width, height, swizzle(original), swizzle(ps))
    rows = [",".join(map(str, pack[i:i + 64])) for i in range(0, len(pack), 64)]
    args.output.write_text("#pragma once\nstatic const unsigned char kControllerIconData[]={\n"
                           + ",\n".join(rows) + "\n};\n", encoding="ascii")


if __name__ == "__main__":
    main()
