# NNUE training — concrete plan (Phase 3)

The goal of this directory: turn our self-play data into a trained net our engine
loads via `EvalFile`, and beat our own HCE in SPRT. Read `docs/NNUE.md` first.

## The concrete objective

> **Train a `768→256→32→32→1` net that beats the HCE baseline by a clear, SPRT-
> proven margin (target: ≥ +50 Elo) at 5+0.05, Threads=1.**

That margin is deliberately modest — it's the *proof the pipeline works end to
end*. Once a net wins at all, scaling data/epochs and upgrading to HalfKA
(Phase 4) is where the big gains (+300 and up) come from.

## Milestones (each is checkable)

1. **Data, small:** `gen_data` produces ~1M positions. (~a few hours single-thread;
   shard with different seeds across cores, then concatenate.)
2. **Train a net:** `bullet` trains to our format; loss curve goes down.
3. **Sanity:** the net loads (`EvalFile`), and `eval(pos) == -eval(mirror)` still
   holds; eval of a clearly-winning position has the right sign and rough size.
4. **SPRT net vs HCE** (`tools/sprt.ps1`, but with `option.EvalFile` set on the
   patch engine). Iterate data/epochs until ≥ +50 Elo.
5. **Scale + HalfKA (Phase 4):** more data, bigger/king-bucketed net, SIMD.

## Step 1 — generate data (no external deps; uses our engine)

`gen_data` (built from `engine/datagen/gen_data.cpp`) self-plays at a fixed node
budget and writes `<fen> | <cp> | <result>` (cp is White-relative; result is
White's 1.0/0.5/0.0). Quiet positions only (not in check, best move not a capture).

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
# one shard: 20000 games, 5000 nodes/move, seed 1
C:\chess_build\bin\gen_data.exe C:\chess_sprt\data\shard1.txt 20000 5000 1
# run several shards in parallel with different seeds + files, then:
Get-Content C:\chess_sprt\data\shard*.txt | Set-Content C:\chess_sprt\data\all.txt
```
Knobs in `gen_data.cpp`: `RANDOM_PLIES` (opening diversity), node budget (quality
vs speed), adjudication thresholds. More nodes = better labels, slower.

## Step 2 — train with `bullet`

[`bullet`](https://github.com/jw1912/bullet) is the modern NNUE trainer (Rust,
CUDA). Install Rust + a CUDA toolkit, clone bullet. Our data is plain text
`fen | cp | wdl`; either use bullet's text/bulletformat loader or convert once
(a ~30-line script: parse our lines → bullet's binary record {features, cp, wdl}).

Architecture to configure in the bullet trainer (must match `nnue.cpp`):
- input: 768 features (`feature_index` scheme: `colorBucket*384 + (pt-1)*64 + sq`,
  black's perspective mirrored `sq^56`, own/enemy buckets — see `nnue.hpp`)
- feature transformer → **256** (perspective; concatenated to 512)
- hidden `512→32→32→1`, clipped-ReLU (clip at 127)
- quantization: FT int16, hidden int8, biases int32 (scales: QA=127, see nnue.cpp)
- loss: blend of eval (cp via sigmoid) and game result (wdl), standard NNUE recipe

## Step 3 — export to our format

`bullet` won't write our format, so add a tiny exporter (Python) that dumps weights
in the exact layout `nnue.cpp::load()` reads (magic `NN01`, then the int16/int8
arrays in order). `tools/make_test_net.py` already writes this format with dummy
weights — copy its `struct.pack` layout, substituting trained weights and the
trainer's quantization. Verify the engine loads it and the symmetry sanity holds.

## Step 4 — SPRT the net vs HCE

```powershell
# build both: patch = engine with EvalFile set; base = same engine, no EvalFile
# (edit tools/sprt.ps1 to add `option.EvalFile=C:\...\net.nnue` to the patch -each,
#  or run fastchess directly with per-engine options)
```
Keep iterating data volume / training epochs / net size until SPRT clears +50 Elo.

## Notes / gotchas

- **Licence:** our own data + own net = clean (not GPL). Don't import SF nets.
- **Format must match exactly:** any mismatch between the exporter and `nnue.cpp`
  layout/quantization = garbage eval. The symmetry sanity check catches gross bugs.
- **Phase 2 first (recommended):** make the accumulator incremental in make/unmake
  before large-scale SPRT, or NNUE eval is ~10x slower than HCE and the match is
  unfair to the net (it'll search far fewer nodes). The from-scratch refresh in
  Phase 1 is correct but slow.
- Data files are git-ignored (`*.txt` under data dirs are large) — keep them under
  `C:\chess_sprt\data`, outside the repo.
