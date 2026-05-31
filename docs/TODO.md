# TODO / Roadmap for future sessions

Guidance for future Claude sessions picking up this engine. Read `CLAUDE.md`
first (decisions, build commands, the ASCII-build-path gotcha, the "user codes
along" style, file-purpose convention). Items are roughly priority-ordered
within each section. Each carries *what / why / how / risks*.

## Status (what already works)
Bitboards + magic sliders, perft-validated move generation, make/unmake +
Zobrist, FEN, **expanded eval** (PeSTO tapered + mobility / bishop pair / rook
open file / pawn structure), **modern search** (PVS, iterative deepening,
aspiration, TT kept across moves, quiescence + **SEE** pruning, move ordering,
null move, RFP/futility/LMP, log-LMR, check extensions), **interruptible async
search** (worker thread + `stop_search`/`clear_stop`), **opening book** (own
Zobrist), **draw detection** (threefold + 50-move, in search and GUI), Qt GUI
(legal-move-enforcing, contrasting pieces). Strength: comfortably > 2000.

## Strength
- [ ] **NNUE evaluation** — the big leap. Write incremental inference (accumulator
      updated in make/unmake, int16 quantized, SIMD-friendly). Needs a *net*:
      train one (e.g. the `bullet` trainer) or use a **permissively-licensed**
      net (Stockfish nets are GPLv3 — using them relicenses us). Keep the eval
      behind the `evaluate()` interface so it's a drop-in. Biggest single
      strength gain; significant effort.
- [ ] **SEE in move ordering** — currently SEE is only a quiescence filter. Order
      captures by SEE (good/equal above killers, losing captures last) and add
      SEE-based pruning of losing captures at low depth in the main search.
      Watch cost: SEE per move at every node is expensive — measure NPS.
- [ ] **Eval tuning (Texel)** — tune the hand-crafted term weights against a
      labelled position set. Cheap-ish, measurable Elo. (Skip if we go NNUE.)
- [ ] **More search heuristics** — singular extensions, internal iterative
      reductions, counter-move history, history gravity/aging, improving-flag
      logic. Each small; validate with SPRT (see Infra).

## Speed (NPS / depth)
- [ ] **Pin/check-aware legal move generation** — #1 NPS win. Today
      `generate_legal` does make/unmake per pseudo-move to test legality (~2x
      work). Generate only legal moves directly (compute pinned pieces + check
      evasions). Substantial, bug-prone rewrite — **re-validate with perft**.
- [ ] **Lazy SMP (multithreading)** — use all cores; ~+70-100 Elo per core
      doubling at first. Design: N threads search the same position, **sharing
      the global TT** (lockless: tolerate torn writes, key validates reads);
      each thread gets its **own `Position` copy + killers/history**. UCI option
      `Threads`. Main thread returns the move; on finish, `stop_search()` halts
      helpers, then join. Note the `g_stop`/`clear_stop` contract (clear on the
      controlling thread before launching). Likely change `Searcher` to hold
      `Position` by value. Concurrency change — dedicated session + careful tests
      (Threads=1 must stay identical).

## Features
- [ ] **Polyglot `.bin` book support** — to use downloadable books. Requires
      embedding Polyglot's exact 781-entry Zobrist array (verify against the
      published start-position key `0x463b96181691fc9c`) and decoding Polyglot's
      move encoding. Our current book uses our own Zobrist (no `.bin` compat).
- [ ] **Proper time management** — allocate from `wtime/btime/winc/binc/
      movestogo` with soft/hard limits; don't start an iteration that can't
      finish (predict from branching factor). Today it's a flat per-move cap.
- [ ] **Insufficient-material draws** (KvK, KvKN, KvKB, KNvK) in engine + GUI.
- [ ] **Pondering** (search on the opponent's clock) — optional.
- [ ] **Syzygy endgame tablebases** — optional, large dependency.

## GUI (Qt)
- [ ] **Highlight legal moves** when a piece is selected (query via chess_core
      `generate_legal`, which the GUI already links).
- [ ] Move list / PGN pane, a clock, board-flip toggle, takeback/undo,
      load/save FEN & PGN, a result banner.
- [ ] Show engine score/PV more richly (currently a raw `info` line in the log).

## Infra / build / distribution
- [ ] **SPRT testing harness** (cutechess-cli) to measure the Elo of each change
      — essential before trusting strength tweaks. Add to `tools/`.
- [ ] **`windeployqt`** to bundle Qt DLLs next to `chess_gui.exe` so it runs
      without MSYS2 on PATH (and consider moving the project to an ASCII path so
      CMake presets work and the moc gotcha disappears — see CLAUDE.md).
- [ ] Expand perft suite (more reference positions) and consider a unit-test
      framework (doctest) if `core_tests.cpp` grows large.
- [ ] CI (GitHub Actions) building + running tests, if the project goes remote.

## Known gotchas (don't regress)
- Build in an **ASCII path** (`C:/chess_build`) — Qt `moc` fails on the accented
  source path. New CMake *targets* (from `if(EXISTS ...)`) need a reconfigure.
- The first stdin line a fresh engine process receives can be dropped by some
  test harnesses (PowerShell pipes / first `WriteLine`); real GUIs send `uci`
  first so it's harmless. Don't "fix" this in the engine.
- Headers under `include/chess/` are definitions only; all tests live in
  `tests/core_tests.cpp`. Don't reintroduce per-header `*_check.cpp`.
