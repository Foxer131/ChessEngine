# References

## Primary
- **Chess Programming Wiki** — https://www.chessprogramming.org/Main_Page
  The canonical reference for everything here.

### Key wiki pages
- Board / bitboards: https://www.chessprogramming.org/Bitboards
- Magic bitboards: https://www.chessprogramming.org/Magic_Bitboards
- Move generation: https://www.chessprogramming.org/Move_Generation
- Perft (move-gen correctness): https://www.chessprogramming.org/Perft
- Perft reference results: https://www.chessprogramming.org/Perft_Results
- Search: https://www.chessprogramming.org/Search
- Alpha-beta: https://www.chessprogramming.org/Alpha-Beta
- Iterative deepening: https://www.chessprogramming.org/Iterative_Deepening
- Transposition table: https://www.chessprogramming.org/Transposition_Table
- Quiescence search: https://www.chessprogramming.org/Quiescence_Search
- Move ordering: https://www.chessprogramming.org/Move_Ordering
- PVS / NegaScout: https://www.chessprogramming.org/Principal_Variation_Search
- Zobrist hashing: https://www.chessprogramming.org/Zobrist_Hashing
- NNUE: https://www.chessprogramming.org/NNUE
- UCI protocol: https://www.chessprogramming.org/UCI

## Engines to study
- **Stockfish** (classical + NNUE, CPU): https://github.com/official-stockfish/Stockfish
  Gold standard for the paradigm we're building.
- **Leela Chess Zero / Lc0** (neural MCTS, GPU): https://github.com/LeelaChessZero/lc0
  The *other* paradigm — relevant only if we ever pursue GPU/NN.

## Tooling
- **Cutechess** (engine-vs-engine, SPRT testing): https://github.com/cutechess/cutechess
- **Arena GUI**: http://www.playwitharena.de/

## Key takeaways from initial study (2026-05-30)
- GPU does **not** help classical alpha-beta engines (sequential, branchy).
  GPU only helps the Lc0 deep-net + MCTS paradigm. NNUE is deliberately CPU/int16.
- Perft is the non-negotiable correctness gate for move generation.
- Move ordering is what makes alpha-beta actually fast.
