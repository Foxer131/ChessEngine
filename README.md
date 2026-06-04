# Chess Engine

A classical, bitboard-based chess engine in modern C++ (C++20) with an **NNUE**
neural-network evaluation, plus a Qt desktop GUI. The engine is a standalone
**UCI** process; the GUI talks to it over UCI, so it also runs in external GUIs
(Cutechess, Arena, fastchess).

> **Design note on GPU:** this is a CPU engine. Alpha-beta search is sequential
> and branchy and gets little from a GPU; NNUE inference is integer SIMD on the
> CPU. Only *training* the net uses a GPU (offline). See
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## What it has

- **Search:** alpha-beta / PVS, iterative deepening, aspiration windows, a shared
  lockless transposition table, quiescence + SEE, move ordering
  (TT/MVV-LVA/killers/history/counter-moves), null-move, RFP, futility, LMP, LMR,
  check extensions. **Lazy SMP** multithreading (UCI `Threads`).
- **Direct legal move generation** (checkers + pinned filter, no make/unmake per
  move) — perft-validated on the five Chess Programming Wiki positions.
- **Two evaluations, switchable at runtime** (UCI `Eval` option / GUI menu):
  - **NNUE** (default) — a `(768→256)x2→1` SCReLU net, trained with `bullet` on
    public Leela/Stockfish data, embedded in the binary, AVX2-optimized with an
    incremental accumulator. It is the stronger eval.
  - **HCE** — the original hand-crafted evaluation (PeSTO PSQT + mobility + pawn
    structure …), kept as a selectable option.
- **Qt GUI** that enforces legal moves, detects mate/stalemate, and drives the
  engine over UCI.

See **[docs/NNUE.md](docs/NNUE.md)** for the full NNUE story (data → training →
+Elo milestones) and **[docs/TODO.md](docs/TODO.md)** for what's next.
**Resuming the project after a pause? Start with
[docs/PROJECT_STATE.md](docs/PROJECT_STATE.md)** — the final handoff (state,
build/run, and which training data was deleted to save disk + how to regenerate it).

## Layout

```
engine/      core library (chess_core) + UCI executable + datagen
  include/   public headers (definitions only)
  src/core   bitboards, board state, zobrist
  src/movegen magic bitboards, legal move generation, make/unmake
  src/search alpha-beta, transposition table, ordering, quiescence, Lazy SMP
  src/eval   evaluation: HCE (eval.cpp) + NNUE (nnue.cpp) + embedded net
  src/uci    UCI protocol loop
  datagen/   self-play training-data generator (gen_data)
gui/         Qt front-end (separate process, talks UCI)
tests/       perft + unit tests (core_tests.cpp)
tools/       SPRT harness, NNUE training/cloud scripts, embed_net.py
docs/        ARCHITECTURE, NNUE, TODO, ROADMAP, REFERENCES
```

## Build (Windows / MSYS2)

> **Important (this machine):** build in an **ASCII path** outside the source
> tree — Qt's `moc` fails under the accented source path. Don't use the CMake
> presets here for that reason (see [CLAUDE.md](CLAUDE.md)).

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
cmake -G Ninja -S . -B C:/chess_build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
cmake --build C:/chess_build
```

Binaries land in `C:\chess_build\bin\` (`engine.exe`, `chess_gui.exe`,
`core_tests.exe`). Run the GUI from that folder so it finds `engine.exe` beside it.

Configure options: `-DCHESS_BUILD_GUI=ON/OFF`, `-DCHESS_BUILD_TESTS=ON/OFF`,
`-DCHESS_BUILD_BENCH=ON/OFF`, `-DCHESS_NATIVE_ARCH=ON/OFF`. A headless build
(engine only, no Qt) uses `-DCHESS_BUILD_GUI=OFF -DCHESS_BUILD_TESTS=OFF`.

## Testing strength

Never trust a strength change without an SPRT — several "obvious" wins measured
neutral or negative. The harness (`tools/`, `tools/training/sprt_nnue.ps1`) uses
**fastchess**. Use **fixed-nodes** SPRT to compare *evaluations* (isolates quality
from speed); use **wall-clock** SPRT (on an idle machine) to judge real strength.
