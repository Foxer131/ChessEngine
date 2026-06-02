# CLAUDE.md — guidance for AI coding sessions

## What this project is
A classical, **bitboard-based** chess engine in **modern C++ (C++20)**, plus a
**Qt** desktop GUI. The engine is a standalone **UCI** process; the GUI is a
separate process that drives it over UCI. CPU-only — see the GPU note below.

**Next steps / open work: see [docs/TODO.md](docs/TODO.md)** (prioritized, with
implementation notes). docs/ROADMAP.md is the original build order.

## Current state & how to get productive fast (read this first)
Where the engine is *today* (so a new session can pick up TODOs without re-deriving it):

- **Strength & search:** working alpha-beta/PVS engine (TT, iterative deepening,
  aspiration, quiescence+SEE, killers/history/counter-moves, null-move, RFP,
  futility, LMP, LMR, check extensions, **singular extensions**). Singular: at
  `depth>=10`, re-search excluding the TT move at `(depth-1)/2` under a `3*depth`
  margin; if all others fail low, extend it +1 ply. SPRT **+26 fixed-nodes / +31
  wall-clock**. The *untuned* v1 (`depth>=8`, `2*depth`) was **−27** — a textbook
  SPRT-over-eyeball save (it drew the Magnus bot beautifully, yet measured worse;
  only the tighter trigger flipped it positive). Quiescence uses a captures-only
  generator (`generate_legal_captures`) — **+30 Elo** by not generating/sorting
  the quiets it discarded. **Lazy SMP multithreading is done**
  (UCI `Threads`; SPRT-measured **+127 Elo** 8-vs-1 thread). Architecture: search
  lives in `engine/src/search/search.cpp` as a `Worker` (all per-thread state) +
  a shared lockless `TranspositionTable`, injected via `SharedState`
  (dependency injection). **Add new search heuristics as `Worker` fields** — you
  then never reason about threads. Threads=1 must stay bit-identical.
- **Move generation is DIRECT-LEGAL** (`engine/src/movegen/movegen.cpp`): computes
  checkers + pinned pieces and filters by mask/ray — no make/unmake per move
  (except en-passant, verified once). **SPRT-measured +163 Elo** vs the old
  pseudo+make/unmake filter (it ~2x'd NPS, especially with NNUE). Uses
  `between_bb`/`line_bb` tables. **Perft is the correctness gate** — the 5 CPW
  positions are in `core_tests.cpp` (incl. Kiwipete d4=4085603); never touch
  movegen without re-running them.
- **Evaluation — the engine has BOTH, uses ONE at a time (a runtime switch):**
  `evaluate()` in `engine/src/eval/eval.cpp` does `if (nnue::is_loaded()) NNUE else HCE`.
  Pick via UCI `Eval` option or the GUI "Evaluation" menu.
  - **NNUE is the DEFAULT** (loaded at startup via `nnue::load_embedded()`). It's a
    `(768→256)x2→1` SCReLU net trained with `bullet` on **public Leela/SF data**
    (ODbL, clean licence), embedded in the binary (`tools/embed_net.py`), with an
    incremental **AVX2** accumulator (Lizard SCReLU). **It beats the HCE by ~+237
    Elo at 8+0.08** (and ~+350 at fixed nodes). The public-data net crushed our own
    13.4M self-play net by +240 — data quality/volume was the lever.
  - **HCE** (hand-crafted: material + PeSTO PSQT + safe mobility + bishop pair +
    rook files + pawn structure) — kept as a selectable option (the project's
    classical-eval study; do not delete), now the secondary eval.
  - Full story + how to retrain/scale: **[docs/NNUE.md](docs/NNUE.md)**.
  - Tried-and-reverted classical eval terms (king safety, passed pawns) were SPRT
    neutral/negative — don't re-add without tuning + SPRT. Search-heuristic tweaks
    (improving/continuation-history/history-LMR) also regressed and were reverted.
- **Measuring strength is mandatory and easy:** the **SPRT harness** is built and
  proven (`tools/` + `tools/training/sprt_nnue.ps1`). It uses **fastchess**
  (`C:\tools\fastchess\...`) and stages binaries under an ASCII path. **Never trust
  a strength change without an SPRT** — several "obvious" wins this project tried
  were actually neutral/negative. Use fixed-nodes SPRT to compare evals fairly
  (NNUE is ~3× slower than HCE, so wall-clock conflates quality with speed).
- **Branching:** risky/experimental work goes on its own branch (`experiment/*`),
  commit in small revertible steps, SPRT before merging to `main`. NNUE work is on
  `experiment/nnue`.
- **External (not in the repo):** training lives in `C:\chess_nnue\bullet`
  (the `bullet` trainer + our `examples/chessengine.rs`); datasets/nets under
  `C:\chess_sprt\data`. `gen_data` (self-play) is the data source.

## Decisions already made (don't re-litigate without the user)
- **Paradigm:** classical alpha-beta + bitboards (Stockfish-style), CPU.
  NOT neural-MCTS. NNUE eval is a possible *later* addition (still CPU/int16).
- **GPU:** intentionally NOT used. Alpha-beta is sequential/branchy and does not
  map to GPUs. Only the Lc0 deep-net+MCTS paradigm benefits from GPU. If the
  user revisits GPU, it means switching paradigms — flag that explicitly.
- **Engine ⇄ GUI:** the engine's *thinking* runs in a separate `engine` process
  driven over UCI (Qt `QProcess`). The GUI *does* link `chess_core` for the
  rules/legality (GuiBoard wraps `chess::Position` + `generate_legal`, so the
  board only accepts legal moves) — the UCI decoupling is about the search, not
  the move rules.
- **GUI tech:** Qt (Widgets). Game setup must let the user pick color and
  search depth / time per move.

## Working style (from the user)
- **The user wants to code along.** Do NOT dump large amounts of code unsolicited.
  Explain, propose, scaffold, and write code only for the specific thing asked.
- Favor honest engineering tradeoffs over hype (the GPU discussion is the model).
- Optimization is a primary goal: bitboards, intrinsics, cache-friendly layout.

## Architecture (see docs/ARCHITECTURE.md for detail)
- `chess_core` static lib = all engine logic. Linked by UCI exe, tests, bench.
- `engine` exe = thin UCI loop over `chess_core`.
- `chess_gui` = Qt app, separate process.
- Module map: `core/` (bitboards, board, zobrist), `movegen/` (magics, legal
  moves, make/unmake), `search/` (alpha-beta/PVS, TT, ordering, quiescence),
  `eval/` (HCE + NNUE, switchable), `uci/`.

## Build / test
- **Toolchain (installed via MSYS2):** GCC + CMake + Ninja + Qt6 live in
  `C:\msys64\mingw64\bin`. Prepend that to `PATH` before building:
  `$env:Path = "C:\msys64\mingw64\bin;" + $env:Path`. The GUI also needs the
  **`mingw-w64-x86_64-qt6-svg`** module (for `QSvgRenderer` piece sprites) —
  `pacman -S mingw-w64-x86_64-qt6-svg` if a fresh box is missing it.
- **CRITICAL — build in an ASCII-only path.** The source tree lives under
  `...\João\...` (accented char) + OneDrive. Qt's `moc` cannot *create* files
  under non-ASCII paths, so configure the build dir OUTSIDE the source tree:
  `cmake -G Ninja -S . -B C:/chess_build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/mingw64`
  then `cmake --build C:/chess_build`. Binaries land in `C:\chess_build\bin\`.
  (CMakePresets put the build dir inside the source tree and will hit the moc
  bug — don't use them on this machine until the project moves to an ASCII path.)
- `compile_commands.json` is exported at the build dir for clangd.
- Tests use CTest. **Perft is the primary correctness gate** for move gen.
- **File-purpose convention:** headers under `include/chess/` hold *definitions
  only*; all tests live in `tests/core_tests.cpp` (a `compile_time` namespace of
  `static_assert`s + runtime `CHECK`s in `main`). `engine/src/` holds only real
  implementation. Don't reintroduce per-header `*_check.cpp` scaffolds.
- Engine sources are auto-discovered via `CONFIGURE_DEPENDS` glob in
  `engine/CMakeLists.txt`: just drop `.cpp` files under `engine/src/{core,
  movegen,search,eval}/` and rebuild. The `engine` exe builds once
  `engine/src/uci/main.cpp` exists; `bench` once `engine/bench/bench_main.cpp`
  exists. Until then the engine targets are skipped and only the GUI builds.

## Build order (see docs/ROADMAP.md)
Foundations → move generation (PERFT-correct FIRST) → search → eval → search
refinements → UCI → GUI → multithreading/strength. Never write search before
perft passes.

## Conventions (to establish as code lands)
- C++20. Prefer `std::popcount`/`std::countr_zero` (with intrinsic fallbacks).
- Hot paths: no allocations, make/unmake (not copy), incremental Zobrist.
- Keep engine free of GUI/Qt dependencies.

## Key references
- Chess Programming Wiki: https://www.chessprogramming.org/Main_Page
- More links in docs/REFERENCES.md. Stockfish (study target), Cutechess (testing).
