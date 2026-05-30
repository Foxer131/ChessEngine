# Architecture

## Paradigm

**Classical bitboard engine, CPU-optimized.** Stockfish-style alpha-beta search
with bitboard move generation. *No GPU* — alpha-beta search is sequential and
branchy and does not map to GPUs. (GPUs only help the Lc0-style neural-MCTS
paradigm, which is a deliberately different project. See `REFERENCES.md`.)

## Process model

```
            UCI protocol (stdin/stdout, plain text)
  ┌────────────┐  position / go / bestmove   ┌──────────────────┐
  │  Qt GUI    │ ◄─────────────────────────► │  engine (CLI)    │
  │ (chess_gui)│        via QProcess         │  links chess_core│
  └────────────┘                             └──────────────────┘
```

- **`chess_core`** (static lib): all engine logic. Linked by the UCI exe,
  tests, and benchmarks — never by the GUI.
- **`engine`** (exe): thin UCI loop around `chess_core`. Works with our GUI
  *and* external GUIs (Cutechess, Arena) for testing & rating.
- **`chess_gui`** (Qt exe): launches `engine` as a child process and speaks
  UCI to it. Knows nothing about engine internals.

Decoupling via UCI is the single most important structural decision: it makes
the engine testable in isolation, benchmarkable against other engines, and
swappable behind any GUI.

## Engine module layout (`engine/src/`)

| Module     | Responsibility                                                        |
|------------|-----------------------------------------------------------------------|
| `core/`    | Bitboard types, `Position`/board state, Zobrist hashing, piece/square types |
| `movegen/` | Magic bitboards for sliders, attack tables, legal move gen, make/unmake |
| `search/`  | Iterative deepening, alpha-beta (PVS), transposition table, move ordering, quiescence |
| `eval/`    | Evaluation — hand-crafted (material + PST + pawn/king terms) first; NNUE later |
| `uci/`     | UCI protocol parsing, option handling, time management, `main.cpp`    |

## Performance principles (the whole point)

- **Bitboards everywhere** — 64-bit set operations, `popcount`/`bitscan` via
  compiler intrinsics (`std::popcount`, `__builtin_ctzll`, `_BitScanForward64`).
- **Magic bitboards** for sliding-piece attacks (precomputed lookup via
  multiply-shift index).
- **Make/unmake** with incremental Zobrist key updates (no full board copies in
  the hot path).
- **Cache-friendly data**: small, flat, contiguous structures; a
  cache-aligned transposition table sized to a power of two.
- **Move ordering** dominates alpha-beta efficiency: TT move → captures (MVV-LVA
  / SEE) → killers → history.
- Measure everything with **perft** (correctness) and **bench** (speed, nodes/s).

## Build targets

See top-level `CMakeLists.txt`. Library/exe split keeps one source of truth.
`compile_commands.json` is exported for clangd and future tooling.
