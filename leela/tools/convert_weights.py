#!/usr/bin/env python3
"""Converts the network weights that original Leela compiles in (NN.cpp, NN128.cpp,
NNValue.cpp from github.com/gcp/Leela, commit afe9099) into one binary file read at
startup, so the engine builds in seconds and the network can be replaced.

    python3 convert_weights.py <gcp-Leela checkout> leela-0.11.weights

Format (little endian): "LEELAW1\\0", u32 array count, then per array:
u16 name length, name (ASCII), u32 float count, float32 values.
NN.cpp is the 192-filter policy net (GPU build), NN128.cpp the smaller CPU policy net
(its arrays get a "cpu_" prefix), NNValue.cpp the value net.
"""
import os
import re
import struct
import sys

ARRAY = re.compile(r"const std::array<float, (\d+)> (\w+) = \{\{(.*?)\}\};", re.S)


def arrays(path, prefix=""):
    text = open(path, encoding="utf-8").read()
    for m in ARRAY.finditer(text):
        n, name, body = int(m.group(1)), m.group(2), m.group(3)
        values = [float(v) for v in body.replace("\n", " ").split(",") if v.strip()]
        if len(values) != n:
            sys.exit(f"{path}: {name} has {len(values)} values, expected {n}")
        yield prefix + name, values


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    all_arrays = [*arrays(os.path.join(src, "NN.cpp")), *arrays(os.path.join(src, "NN128.cpp"), "cpu_"),
                  *arrays(os.path.join(src, "NNValue.cpp"))]
    with open(out, "wb") as f:
        f.write(b"LEELAW1\0")
        f.write(struct.pack("<I", len(all_arrays)))
        for name, values in all_arrays:
            f.write(struct.pack("<H", len(name)) + name.encode("ascii"))
            f.write(struct.pack("<I", len(values)))
            f.write(struct.pack(f"<{len(values)}f", *values))
    total = sum(len(v) for _, v in all_arrays)
    print(f"{len(all_arrays)} arrays, {total} floats -> {out}")


if __name__ == "__main__":
    main()
