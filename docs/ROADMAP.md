# Roadmap

Build in this order. Each phase produces something testable before moving on.
The golden rule: **get move generation provably correct (perft) before writing
a single line of search.** Most engine bugs are move-gen bugs.

## Phase 0 — Foundations
- [ ] `core/`: types (Square, Piece, Color, Move), `Bitboard` alias + helpers
      (popcount, bitscan, shifts), pretty-printers.
- [ ] `Position`: board state, side to move, castling rights, en passant, FEN
      parse/print.
- [ ] Zobrist hashing keys.

## Phase 1 — Move generation (correctness first)
- [ ] Non-slider attacks: pawn, knight, king lookup tables.
- [ ] Slider attacks: **magic bitboards** for bishop/rook (queen = both).
- [ ] Legal move generation (handle pins, checks, en passant edge cases, castling).
- [ ] make/unmake move with incremental Zobrist update.
- [ ] **PERFT** against known reference positions (Kiwipete etc.) — must match exactly.

## Phase 2 — Search (make it play)
- [ ] Negamax + alpha-beta.
- [ ] Iterative deepening + basic time management.
- [ ] Quiescence search (captures/promotions) to kill the horizon effect.
- [ ] Transposition table (Zobrist-keyed).
- [ ] Move ordering: TT move → MVV-LVA captures → killers → history.
- [ ] Principal Variation Search (PVS).

## Phase 3 — Evaluation
- [ ] Material + piece-square tables.
- [ ] Pawn structure, king safety, mobility, tapered eval (mid/endgame).
- [ ] (Later) NNUE: efficiently-updatable net, CPU int16 inference.

## Phase 4 — Search refinements (strength)
- [ ] Null-move pruning, late move reductions (LMR), futility/aspiration windows.
- [ ] Check extensions, SEE-based pruning.

## Phase 5 — UCI
- [ ] Full UCI loop: `uci`, `isready`, `position`, `go`, `stop`, `bestmove`.
- [ ] Options: Hash size, threads (later), search depth / move time.
- [ ] Verify against Cutechess / Arena.

## Phase 6 — GUI (Qt)
- [ ] Board widget, drag-and-drop moves, highlights.
- [ ] New-game dialog: choose color, search depth / time per move.
- [ ] Launch `engine` via QProcess; bridge GUI moves ⇄ UCI.

## Phase 7 — Going faster / stronger
- [ ] Lazy SMP multithreaded search.
- [ ] Opening book / endgame tablebases (Syzygy).
- [ ] SPRT testing harness for measuring each change's Elo impact.
