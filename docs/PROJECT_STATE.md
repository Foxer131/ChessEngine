# PROJECT STATE — final handoff (read this to resume)

Snapshot for a future session/agent picking this up after a long pause. Pair it
with `CLAUDE.md` (decisions + build gotchas), `docs/TODO.md` (prioritized next
work), and `docs/NNUE.md` (the eval story). Last active: 2026-06, `main` @ commit
`a8a0001`.

## TL;DR — where the project landed
A working, fairly strong CPU chess engine + Qt GUI:
- **Bitboards + magics**, **direct-legal movegen** (perft-validated, 5 CPW
  positions), make/unmake + Zobrist.
- **Modern search**: PVS, iterative deepening, aspiration, shared lockless TT,
  **captures-only quiescence** + SEE, killers/history/counter-moves, null-move,
  RFP/futility/LMP, log-LMR, check extensions, **singular extensions**. **Lazy
  SMP** (UCI `Threads`).
- **Eval**: **NNUE is the default** (`(768→256)x2→1` SCReLU, AVX2 incremental
  accumulator, **embedded in the binary**), beats the kept hand-crafted **HCE**
  by ~+237 Elo wall-clock. Switchable at runtime (UCI `Eval` / GUI menu).
- **GUI**: Qt, legal-move-enforcing, mate/stalemate detection, eval selector,
  **SVG (cburnett) piece sprites**, **legal-move highlights**, drives the engine
  over UCI in a separate process.
- Strength: drew a 99%-accuracy game vs the chess.com "Magnus" bot at depth 15
  (an eyeball anecdote, not a rating). Real measured gains all went through SPRT.

## Repo & backup
- GitHub: **https://github.com/Foxer131/ChessEngine** (`main` is the live branch;
  several `perf/*`, `feature/*`, `experiment/*` branches are pushed for history).
- Everything important is committed. The working tree is clean.

## How to build & run (Windows / MSYS2)
> Build in an **ASCII path** outside the source tree — Qt `moc` fails under the
> accented `…\João\…` OneDrive path. Binaries land in `C:\chess_build\bin\`.

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
cmake -G Ninja -S . -B C:/chess_build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
cmake --build C:/chess_build
# run the GUI (finds engine.exe beside it):
& "C:\chess_build\bin\chess_gui.exe"
```
Build deps in MSYS2 mingw64: GCC, CMake, Ninja, **Qt6 Widgets + Svg**
(`mingw-w64-x86_64-qt6-svg` — needed for the piece sprites), and Qt6 base. The
engine alone (no Qt) builds with `-DCHESS_BUILD_GUI=OFF -DCHESS_BUILD_TESTS=OFF`.

**The engine/GUI need NO external data to run** — the NNUE net is compiled into
`engine.exe` (`engine/src/eval/embedded_net.cpp`, loaded by `nnue::load_embedded()`).

## Disk layout & what was DELETED to free space (important!)
To reclaim ~40 GB at the end of the project, the **training datasets were deleted**.
They are NOT needed to build or play — only to *retrain* the NNUE. What remains vs
what's gone:

| Path | Status |
|---|---|
| `C:\chess_build\` | **KEPT** — build output (engine.exe, chess_gui.exe). Rebuildable. |
| `C:\chess_sprt\data\*.nnue` | **KEPT** — trained nets (see below). ~2 MB total. |
| `C:\chess_nnue\bullet\` (trainer code + checkpoints) | **KEPT** — minus its data. |
| `C:\tools\fastchess\` | **KEPT** — the SPRT match runner. |
| `C:\chess_nnue\bullet\data\public.binpack` (37.5 GB) | **DELETED** — the ODbL public Leela/SF binpack. |
| `C:\chess_sprt\data\{all,train_norm}.txt`, `*\train.data` | **DELETED** — derived training files. |

**To retrain again you must re-acquire the data:** the public binpack is the
`nodes5000pv2_UHO`-style Leela/Stockfish dataset (ODbL) — re-download from the
public SF/Leela data servers (see `docs/NNUE.md` for the exact dataset lineage),
then regenerate the derived `train.data` via `tools/training/` scripts. Or
generate fresh self-play data with `gen_data` / the cloud pipeline (`tools/cloud`).

## The trained nets (kept, small) — `C:\chess_sprt\data\`
- **`net_pub.nnue`** — the current/best net (public data). **This is what's
  embedded in the binary.** To re-embed after a retrain: `tools/embed_net.py`.
- `net13m.nnue` (+108 vs HCE), `net5m.nnue` (−99), `net_v2boot.nnue`,
  `net.nnue` — historical nets kept for provenance/SPRT baselines.

## What to do next (if resuming)
1. **NNUE data-saturation test** (the planned next experiment): retrain the same
   256 net on ~2× public data, fixed-nodes SPRT vs `net_pub`. Wins → get more data
   (16B binpack); ties → go bigger net (HalfKA) + int8. Full rationale in
   `docs/TODO.md` (Strength) and the saved agent-memory note.
2. Other open items in `docs/TODO.md`: SEE-pruning in main search, lazy eval,
   proper time management (the real fix for "minutes per move" — fixed-depth has
   no clock; EBF is a healthy ~1.8), more GUI polish (clock, PGN pane, takeback).

## Hard-won lessons (don't relearn them the hard way)
- **No strength change ships without an SPRT.** Eyeball impressions have been
  wrong here repeatedly: a gorgeous drawn game vs the Magnus bot came from a
  change that measured **−27 Elo**; only tuning + SPRT flipped it to +26/+31.
  LTO "obviously" should have helped — measured neutral, dropped.
- **Fixed-nodes SPRT** isolates eval/idea quality from speed; **wall-clock** is
  the real-play verdict. Run both for search heuristics; fixed-nodes alone for
  eval changes (NNUE is ~3× slower per eval than HCE).
- Build in an ASCII path; never touch movegen without re-running perft; add new
  search heuristics as `Worker` fields so threading stays a non-issue.
- Piece art: we use the open **cburnett** set (BSD), **not** chess.com's
  proprietary sprites — keep it that way (`gui/resources/pieces/CREDITS.md`).
