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
- [~] **NNUE evaluation** — IN PROGRESS (branch `experiment/nnue`, see docs/NNUE.md).
      Inference + incremental accumulator + `bullet` training pipeline all work.
      Nets so far lose to HCE but data scaling is paying off fast (fixed-nodes SPRT
      vs HCE: 355k pos → −720 Elo; 5M pos → −99 Elo). **Next: more & better data**
      (10–50M positions, and a higher search-node budget per labelled position than
      the current 5000 for cleaner targets), then a bigger net / HalfKA. The
      mechanics are sound; this is now a data+training problem.
- [ ] **Remote (cloud) data generation + training** — the practical bottleneck is
      our laptop (CPU for self-play, one RTX 4050 for training). A cheap cloud box
      makes the NNUE loop far faster and lets it run unattended:
        * **Data gen**: self-play is embarrassingly parallel and CPU-only. Spin up
          a high-core spot VM (or a few), run `gen_data` sharded (see
          `tools/training/gen_shards.ps1`), and store the dataset in object storage
          (S3/GCS/R2). 10–50M positions in well under an hour on 32–64 cores.
        * **Training**: a single cloud GPU (T4/A10/L4, spot) trains our small net in
          minutes; cost is a few reais per run. Host the produced `.nnue` in the
          same bucket; the engine just needs the file for `EvalFile`.
        * **Make it reproducible**: a script/Dockerfile that provisions, generates,
          trains, and uploads the net, so a session can kick it off and poll. Keep
          everything our-own (clean licence). This is the highest-leverage infra
          item for move-quality, since NNUE strength is bounded by data volume.
- [ ] **SEE in move ordering** — currently SEE is only a quiescence filter. Order
      captures by SEE (good/equal above killers, losing captures last) and add
      SEE-based pruning of losing captures at low depth in the main search.
      Watch cost: SEE per move at every node is expensive — measure NPS.
- [ ] **NNUE inference SIMD** — `nnue::forward()` is scalar (~3× slower than HCE).
      AVX2 over the i16 accumulator (and the SCReLU) recovers most of the gap.
      Carefully verified — a wrong SIMD pass silently corrupts eval. Only matters
      once a net is competitive; until then use fixed-nodes SPRT.
- [ ] **Eval tuning (Texel)** — tune the hand-crafted term weights against a
      labelled position set. Cheap-ish, measurable Elo. Still worth it because HCE
      remains the default/stronger eval until NNUE overtakes it.
- [ ] **More search heuristics** — singular extensions, internal iterative
      reductions, history gravity/aging. Each small; validate with SPRT. NOTE:
      improving-flag + continuation-history + history-based LMR were tried together
      and regressed (reverted) — re-attempt individually, tuned, with SPRT.

## Speed (NPS / depth)
- [ ] **Pin/check-aware legal move generation** — #1 NPS win. Today
      `generate_legal` does make/unmake per pseudo-move to test legality (~2x
      work). Generate only legal moves directly (compute pinned pieces + check
      evasions). Substantial, bug-prone rewrite — **re-validate with perft**.
- [x] **Lazy SMP (multithreading)** — DONE (branch `experiment/hardware-smp`).
      N `Worker`s search the same root sharing only a `TranspositionTable`
      (lockless: key-validated, torn writes tolerated). Each Worker owns its
      Position copy + history/killers/counters + repetition list. Shared things
      are injected by reference via `SharedState` (constructor injection) — so
      **adding a new heuristic is a Worker field, never a concurrency problem**.
      UCI option `Threads`; the GUI sets it to cores-1. Threads=1 is bit-identical
      to the old search (verified by equal node counts). Measured +2 depth at
      fixed time with 8 threads. *Future search heuristics should keep landing on
      `Worker` so they stay thread-agnostic.*

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
- [x] **SPRT testing harness** — DONE. `tools/` (build_versions.ps1, sprt.ps1) and
      `tools/training/sprt_nnue.ps1`, using **fastchess**. Stages binaries+book to
      an ASCII path, puts MSYS2 on PATH. Supports fixed-nodes matches. USE IT for
      every strength change.
- [ ] **`windeployqt`** to bundle Qt DLLs next to `chess_gui.exe` so it runs
      without MSYS2 on PATH (and consider moving the project to an ASCII path so
      CMake presets work and the moc gotcha disappears — see CLAUDE.md).
- [ ] Expand perft suite (more reference positions) and consider a unit-test
      framework (doctest) if `core_tests.cpp` grows large.
- [ ] CI (GitHub Actions) building + running tests, if the project goes remote.

## Further strength ideas (each: implement small, SPRT, keep only if it gains)
These are the higher-value items beyond what's listed above, roughly by expected
Elo-per-effort. None are committed; all need an SPRT before trusting.
- [ ] **Better data for NNUE** (the single biggest lever right now): more positions
      AND higher quality labels — score with a real search depth/node budget, filter
      noisy/early positions, dedupe, and mix in positions from games vs varied
      opponents (not just self-play) to reduce bias. See the cloud item above.
- [ ] **Pin/check-aware legal movegen** (also under Speed): ~2× NPS, which is Elo at
      fixed time, and it benefits every search feature. Big, perft-gated rewrite.
- [ ] **Aspiration/time-management tuning**: smarter soft/hard time limits and
      not starting an iteration we can't finish — converts wasted time into depth.
- [ ] **History/heuristic retuning** (the reverted batch, redone right): capture
      history, continuation history, and history-based LMR are standard +Elo in
      strong engines — they regressed here only because they were untuned and
      bundled. Add one at a time with its own SPRT and proper scaling constants.
- [ ] **Correction history / eval blending**: small learned correction to the eval
      based on recent search results; cheap, modern, measurable.
- [ ] **Opening book from real games / bigger book**: our book is tiny. A broader,
      vetted book avoids weak openings (the engine's eval can't fix a bad opening).
- [ ] **Contempt / draw-avoidance knob** for play vs weaker opponents (optional,
      affects match scores not pure strength).

## Known gotchas (don't regress)
- Build in an **ASCII path** (`C:/chess_build`) — Qt `moc` fails on the accented
  source path. New CMake *targets* (from `if(EXISTS ...)`) need a reconfigure.
- The first stdin line a fresh engine process receives can be dropped by some
  test harnesses (PowerShell pipes / first `WriteLine`); real GUIs send `uci`
  first so it's harmless. Don't "fix" this in the engine.
- Headers under `include/chess/` are definitions only; all tests live in
  `tests/core_tests.cpp`. Don't reintroduce per-header `*_check.cpp`.
