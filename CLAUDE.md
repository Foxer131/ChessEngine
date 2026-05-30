# CLAUDE.md — guidance for AI coding sessions

## What this project is
A classical, **bitboard-based** chess engine in **modern C++ (C++20)**, plus a
**Qt** desktop GUI. The engine is a standalone **UCI** process; the GUI is a
separate process that drives it over UCI. CPU-only — see the GPU note below.

## Decisions already made (don't re-litigate without the user)
- **Paradigm:** classical alpha-beta + bitboards (Stockfish-style), CPU.
  NOT neural-MCTS. NNUE eval is a possible *later* addition (still CPU/int16).
- **GPU:** intentionally NOT used. Alpha-beta is sequential/branchy and does not
  map to GPUs. Only the Lc0 deep-net+MCTS paradigm benefits from GPU. If the
  user revisits GPU, it means switching paradigms — flag that explicitly.
- **Engine ⇄ GUI:** decoupled via the UCI protocol (stdin/stdout). The GUI
  (`chess_gui`) launches the `engine` exe (Qt `QProcess`) and never links
  `chess_core`.
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
  `eval/` (HCE now, NNUE later), `uci/`.

## Build / test
- **Toolchain (installed via MSYS2):** GCC + CMake + Ninja + Qt6 live in
  `C:\msys64\mingw64\bin`. Prepend that to `PATH` before building:
  `$env:Path = "C:\msys64\mingw64\bin;" + $env:Path`.
- **CRITICAL — build in an ASCII-only path.** The source tree lives under
  `...\João\...` (accented char) + OneDrive. Qt's `moc` cannot *create* files
  under non-ASCII paths, so configure the build dir OUTSIDE the source tree:
  `cmake -G Ninja -S . -B C:/chess_build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/mingw64`
  then `cmake --build C:/chess_build`. Binaries land in `C:\chess_build\bin\`.
  (CMakePresets put the build dir inside the source tree and will hit the moc
  bug — don't use them on this machine until the project moves to an ASCII path.)
- `compile_commands.json` is exported at the build dir for clangd.
- Tests use CTest. **Perft is the primary correctness gate** for move gen.
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
