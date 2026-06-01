#!/usr/bin/env python3
"""Normalize the result token in a `fen | cp | result` file to bullet's 1.0/0.5/0.0
form. Usage: python normalize_results.py <in.txt> <out.txt>"""
import sys, collections

inp, out = sys.argv[1], sys.argv[2]
c = collections.Counter()
n = 0
with open(inp) as f, open(out, "w") as g:
    for line in f:
        line = line.rstrip("\n")
        if not line:
            continue
        head, _, res = line.rpartition("|")
        res = res.strip()
        if res in ("1", "1.0"):   res = "1.0"
        elif res in ("0", "0.0"): res = "0.0"
        else:                     res = "0.5"
        g.write(f"{head.rstrip()} | {res}\n")
        c[res] += 1
        n += 1
print(f"wrote {n} lines -> {out}")
print("result distribution:", dict(c))
