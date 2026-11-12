import hashlib
import mmap
from pathlib import Path
import re
import struct
import sys
import zlib


def words(data, offset, count=1):
    return struct.unpack_from("<" + "I" * count, data, offset)


def models(archive):
    with archive.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
        base, payload = words(data, 16, 2)
        count = words(data, 32)[0]
        for index, (address,) in enumerate(struct.iter_unpack("<I", data[48:48 + count * 4])):
            offset, _, size, packed = words(data, base + address, 4)
            start = base + payload + offset
            if not packed or size < 100:
                continue
            try:
                if not zlib.decompressobj().decompress(data[start:start + min(packed, 1024)], 16).startswith(b"TMC\0"):
                    continue
                model = zlib.decompress(data[start:start + packed])
            except zlib.error:
                continue
            nodes = set(re.findall(rb"MOTryu[0-9a-zA-Z_]+", model))
            if len(nodes) >= 5:
                yield index, model, nodes


if __name__ == "__main__":
    output = Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    for index, model, nodes in models(Path(sys.argv[1])):
        name = model[0x50:0x70].split(b"\0")[0].decode("ascii", "replace")
        print(index, name, len(model), len(nodes), hashlib.sha256(model).hexdigest(), flush=True)
        (output / f"{index:05}.tmc").write_bytes(model)
