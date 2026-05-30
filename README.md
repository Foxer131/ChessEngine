# Chess Engine

A classical, bitboard-based chess engine written in modern C++ (C++20), with a
Qt desktop GUI. The engine is a standalone **UCI** process; the GUI talks to it
over the UCI protocol, so the engine also works inside external GUIs
(Cutechess, Arena).

> **Design note on GPU:** this is a CPU engine. Classical alpha-beta search is
> sequential and branchy and gets little from a GPU — GPUs only help the
> neural-MCTS (Lc0) paradigm. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Layout

```
engine/      core library (chess_core) + UCI executable + benchmarks
  include/   public headers
  src/core   bitboards, board state, zobrist
  src/movegen magic bitboards, move generation, make/unmake
  src/search alpha-beta, transposition table, ordering, quiescence
  src/eval   evaluation (hand-crafted now, NNUE later)
  src/uci    UCI protocol loop
gui/         Qt front-end (separate process, talks UCI)
tests/       perft + unit tests
tools/       scripts (magic generation, tuning, SPRT harness)
docs/        ARCHITECTURE, ROADMAP, REFERENCES
```

## Build

```sh
cmake --preset release
cmake --build --preset release
```

Configure options: `-DCHESS_BUILD_GUI=ON/OFF`, `-DCHESS_BUILD_TESTS=ON/OFF`,
`-DCHESS_NATIVE_ARCH=ON/OFF`.

## Status

Early scaffolding. See [docs/ROADMAP.md](docs/ROADMAP.md) for the build order.
The first milestone is **perft-correct move generation**.
