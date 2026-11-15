#!/usr/bin/env python3
"""Packs leela-0.11.weights (LEELAW1, float32) into smaller LEELAW2 files.

    python3 pack_weights.py leela-0.11.weights leela-0.11-fp16.weights fp16
    python3 pack_weights.py leela-0.11.weights leela-0.11-q8.weights q8
    python3 pack_weights.py leela-0.11.weights leela-0.11-q6.weights q6

fp16  every value as IEEE half precision (plays the same as the original)
qN    weights (*_w) as N-bit integers, symmetric, one float32 scale per output
      channel; biases (*_b) stay float32. A value is q * scale of its channel.

LEELAW2 (little endian): "LEELAW2\\0", u32 array count, then per array: u16 name
length, name, u32 value count, u8 encoding (0 float32, 1 float16, 2 quantized:
u8 bits, u32 output channels, float32 scales, then the values as `bits`-bit two's
complement, LSB first). Read by src/NNWeights.cpp.

The arithmetic follows float32 (scale = max|w| / qmax, q = round-half-even(w / scale)),
so the files hold exactly the values measured in the size/strength tests:
fp16 and 8-bit play like the original; 6-bit is clearly weaker.
"""
import array
import struct
import sys


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def read_v1(path):
    d = open(path, "rb").read()
    if d[:8] != b"LEELAW1\0":
        sys.exit(f"{path}: not a LEELAW1 file")
    count = struct.unpack_from("<I", d, 8)[0]
    p, arrays = 12, []
    for _ in range(count):
        ln = struct.unpack_from("<H", d, p)[0]
        name = d[p + 2:p + 2 + ln].decode()
        p += 2 + ln
        n = struct.unpack_from("<I", d, p)[0]
        p += 4
        a = array.array("f")
        a.frombytes(d[p:p + 4 * n])
        if sys.byteorder != "little":
            a.byteswap()
        arrays.append((name, a))
        p += 4 * n
    return arrays


def quantize(values, channels, bits):
    qmax = 2 ** (bits - 1) - 1
    per = len(values) // channels
    scales, qs = [], []
    for c in range(channels):
        row = values[c * per:(c + 1) * per]
        scale = f32(max(abs(v) for v in row) / qmax) or 1.0
        scales.append(scale)
        qs += [max(-qmax, min(qmax, round(f32(v / scale)))) for v in row]
    acc = nbits = 0
    out = bytearray()
    mask = (1 << bits) - 1
    for q in qs:
        acc |= (q & mask) << nbits
        nbits += bits
        while nbits >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nbits -= 8
    if nbits:
        out.append(acc & 0xFF)
    return struct.pack("<BI", bits, channels) + struct.pack(f"<{channels}f", *scales) + bytes(out)


def main():
    if len(sys.argv) != 4 or sys.argv[3] not in ("fp16", "q8", "q6", "q5", "q4"):
        sys.exit(__doc__)
    src, dst, kind = sys.argv[1:]
    arrays = read_v1(src)
    channels = {name: len(a) for name, a in arrays if name.endswith("_b")}
    with open(dst, "wb") as f:
        f.write(b"LEELAW2\0" + struct.pack("<I", len(arrays)))
        for name, a in arrays:
            f.write(struct.pack("<H", len(name)) + name.encode() + struct.pack("<I", len(a)))
            if kind == "fp16":
                f.write(b"\x01" + struct.pack(f"<{len(a)}e", *a))
            elif name.endswith("_w"):
                f.write(b"\x02" + quantize(a, channels[name[:-2] + "_b"], int(kind[1:])))
            else:
                f.write(b"\x00" + struct.pack(f"<{len(a)}f", *a))
    print(f"{dst}: {kind}")


if __name__ == "__main__":
    main()
