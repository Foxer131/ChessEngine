#!/usr/bin/env python3
"""Write a tiny DETERMINISTIC test network in our NNUE-lite format (magic NN01).

This is NOT a trained net - it exists only to exercise the C++ inference pipeline
end to end (file format, load, refresh, forward, the evaluate() seam) before any
real training. Weights are small fixed pseudo-random values so the eval is finite
and varies with the position. See docs/NNUE.md for the format.

Usage:  python tools/make_test_net.py C:/chess_sprt/run/test.nnue
"""
import struct, sys

INPUT_DIM = 768
L1 = 256
H1 = 32
H2 = 32

# Cheap deterministic generator (no numpy dependency): a small LCG, values in a
# tight range so accumulator/intermediate sums stay well inside int16/int32.
_state = 0x1234_5678
def rnd(lo, hi):
    global _state
    _state = (_state * 1103515245 + 12345) & 0x7FFFFFFF
    return lo + (_state % (hi - lo + 1))

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "test.nnue"
    with open(path, "wb") as f:
        f.write(b"NN01")
        f.write(struct.pack("<i", L1))
        # feature transformer: int16 weights in [-8,8], small int16 biases
        f.write(struct.pack(f"<{INPUT_DIM*L1}h", *[rnd(-8, 8) for _ in range(INPUT_DIM*L1)]))
        f.write(struct.pack(f"<{L1}h", *[rnd(-16, 16) for _ in range(L1)]))
        # hidden 1: int8 weights, int32 biases
        f.write(struct.pack(f"<{2*L1*H1}b", *[rnd(-4, 4) for _ in range(2*L1*H1)]))
        f.write(struct.pack(f"<{H1}i", *[rnd(-64, 64) for _ in range(H1)]))
        # hidden 2
        f.write(struct.pack(f"<{H1*H2}b", *[rnd(-4, 4) for _ in range(H1*H2)]))
        f.write(struct.pack(f"<{H2}i", *[rnd(-64, 64) for _ in range(H2)]))
        # output
        f.write(struct.pack(f"<{H2}b", *[rnd(-8, 8) for _ in range(H2)]))
        f.write(struct.pack("<i", rnd(-64, 64)))
    print(f"wrote {path}")

if __name__ == "__main__":
    main()
