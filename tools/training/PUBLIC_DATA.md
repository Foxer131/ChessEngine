# Training on public datasets (more data = better eval, all modes)

Our self-play gave 13.4M positions (beats HCE +108 at fixed nodes). Public chess
datasets are **100x–1000x bigger** and labelled at high quality - the proven lever
to push the net further. Licence is clean: Stockfish's training data comes from
**Leela Chess Zero (Open Database License)**, reusable by any engine; Lichess DB is
CC0. We train our OWN net from it, so nothing is encumbered.

## Get a dataset (binpack, loaded natively by bullet)
Direct download links (from the nnue-pytorch wiki "Training datasets"; all are
Stockfish/Leela binpacks, Leela data is ODbL). **Smallest/first-pick at the top.**

| Dataset | Labels | Google Drive link |
|---|---|---|
| `nodes5000pv2_UHO.binpack` (recommended first) | 5000 nodes, UHO openings | https://drive.google.com/file/d/1UQdZN_LWQ265spwTBwDKo0t1WjSJKvWY/view |
| `dfrc_n5000.binpack` | 5000 nodes (DFRC) | https://drive.google.com/file/d/17vDaff9LAsVo_1OfsgWAIYqJtqR8aHlm/view |
| `training_data.binpack` | mixed | https://drive.google.com/file/d/1RFkQES3DpsiJqsOtUshENtzPfFgUmEff/view |
| `large_gensfen_multipvdiff_100_d9.binpack` | depth 9 | https://drive.google.com/file/d/1VlhnHL8f-20AXhGkILujnNXHwy9T-MQw/view |
| `T60T70wIsRightFarseer.binpack` | mixed Leela | https://drive.google.com/file/d/1_sQoWBl31WAxNXma2v45004CIVltytP8/view |
| `data_d9_2021_09_02.binpack` (~16B positions, huge) | depth 9 | https://drive.google.com/file/d/1lFC_tej8WyXojhh7-AmXV_kkrTauEsqt/view |

Easiest: open the link in a browser and download, then move/rename it to
`C:\chess_nnue\bullet\data\public.binpack`.

CLI alternative (big files need gdown to handle Drive's virus-scan confirmation):
```powershell
pip install gdown
# the long token in the link is the FILE_ID (e.g. 1UQdZN_LWQ265spwTBwDKo0t1WjSJKvWY)
gdown 1UQdZN_LWQ265spwTBwDKo0t1WjSJKvWY -O C:\chess_nnue\bullet\data\public.binpack
```
(Some links are tens of GB. `data_d9_2021_09_02` is the 16B-position monster - only
if you have the disk + time; start with `nodes5000pv2_UHO`.)

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
