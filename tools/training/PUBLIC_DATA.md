# Training on public datasets (more data = better eval, all modes)

Our self-play gave 13.4M positions (beats HCE +108 at fixed nodes). Public chess
datasets are **100x–1000x bigger** and labelled at high quality - the proven lever
to push the net further. Licence is clean: Stockfish's training data comes from
**Leela Chess Zero (Open Database License)**, reusable by any engine; Lichess DB is
CC0. We train our OWN net from it, so nothing is encumbered.

## Get a dataset (binpack, loaded natively by bullet)
From the nnue-pytorch wiki "Training datasets"
(<https://github.com/official-stockfish/nnue-pytorch/wiki/Training-datasets>) or
Kaggle (<https://www.kaggle.com/datasets/linrock>). Examples:
- `nodes5000pv2_UHO.binpack` — moderate size, depth-ish 5000 nodes (good first pick).
- `data_d9_2021_09_02.binpack` — ~16B positions, depth 9 (huge; only if you have the
  disk + time).

Download one and put it at:
```
C:\chess_nnue\bullet\data\public.binpack
```
(Large files come from Google Drive links on the wiki - download in a browser.)

## Train (same arch, so the engine loads it unchanged)
```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
$env:CUDA_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
$env:Path = "$env:CUDA_PATH\bin;" + $env:Path
cd C:\chess_nnue\bullet
cargo r -r --features cuda --example chessengine_public
```
`chessengine_public.rs` reads `data/public.binpack` via bullet's native
`SfBinpackLoader`, filters to quiet positions (skip in-check / capture-best-move /
extreme scores), and trains `(768->256)x2->1` SCReLU, QA=255 QB=64 - identical to
our embedded format. Output: `checkpoints/chessengine_public-*/quantised.bin`.

## Deploy + measure
```powershell
Copy-Item C:\chess_nnue\bullet\checkpoints\chessengine_public-240\quantised.bin C:\chess_sprt\data\net_pub.nnue
# SPRT the new net vs the current embedded net13m, fixed nodes (isolates quality):
#   fastchess ... -engine ...EvalFile=net_pub.nnue ... -engine ...EvalFile=net13m.nnue ... nodes=20000
```
If it gains, regenerate `embedded_net.cpp` from it (`tools/embed_net.py`) and rebuild.

## Then: int8 + maddubs (speed, to also win on the clock)
Separate, careful step (the forward dominates our cost; int8 ~2x it):
1. In the trainer, quantise the OUTPUT layer to **i8** (`.quantise::<i8>(QB)`), keep
   the feature transformer i16. bullet clips output weights to fit.
2. In `nnue.cpp`, change `outW` to int8 and rewrite `dot_screlu` with
   `_mm256_maddubs_epi16` (uint8 activation x int8 weight -> int16, 32/instr) then
   `_mm256_madd_epi16` to widen+sum. Watch the unsigned/signed operand rule and the
   QA clamp (clamp to 127 in the int8 path, not 255).
3. Validate bit-exactness is NOT expected (int8 rounds differently) - instead verify
   the net still beats HCE at fixed nodes, then re-run the wall-clock SPRT.
This is the path to flip NNUE default for TIMED play too. Until then, the GUI already
auto-uses NNUE for depth-limited games (where it's stronger now).
