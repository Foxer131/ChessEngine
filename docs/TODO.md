# TODO / Roadmap for future sessions

Guidance for future Claude sessions picking up this engine. Read `CLAUDE.md`
first (decisions, build commands, the ASCII-build-path gotcha, the "user codes
along" style, file-purpose convention). Items are roughly priority-ordered
within each section. Each carries *what / why / how / risks*.

## Status (what already works)
Bitboards + magic sliders, **direct legal move generation** (checkers+pinned,
perft-validated on 5 CPW positions; +163 Elo over the old filter), make/unmake +
Zobrist, FEN, **NNUE evaluation (default, beats HCE ~+237 Elo wall-clock)** with
the hand-crafted **HCE kept as a selectable option**, **modern search** (PVS,
iterative deepening, aspiration, shared lockless TT, quiescence + **SEE**, move
ordering, null move, RFP/futility/LMP, log-LMR, check extensions), **Lazy SMP**
(UCI `Threads`, +127 Elo 8v1), **opening book**, **draw + mate/stalemate
detection** (engine + GUI), Qt GUI (legal-move-enforcing, eval selector). The
NNUE training + cloud-datagen pipeline is built. Strength: well above the old
HCE-only engine.

## Strength
- [x] **NNUE evaluation — DONE and is the DEFAULT** (on `main`, see docs/NNUE.md).
      Full pipeline: self-play datagen (`gen_data`), cloud generation
      (`tools/cloud`), `bullet` training, our NN01 inference with incremental AVX2
      accumulator, embedded in the binary (`tools/embed_net.py`), UCI `Eval` + GUI
      switch. The current net (`net_pub`) is trained on **public Leela/SF data**
      (ODbL) and beats HCE **~+237 Elo wall-clock** (+350 fixed-nodes); public data
      beat our 13.4M self-play net by +240. Data quality/volume was the lever.
- [ ] **NNUE next steps** (to push further):
        * **More / bigger public data** (the 16B `data_d9` binpack) and **bigger
          net / HalfKA king-buckets** — the next quality jump (HalfKA wants int8).
        * **int8 + `maddubs`** forward for more nodes/sec (forward dominates cost).
        * **Bootstrapping** is now valid — label new data with `net_pub`
          (`gen_data <...> <net>` / `gen_shards -Net`), retrain, SPRT.
- [x] **Remote (cloud) data generation** — DONE. `tools/cloud/` (GCP spot VM:
      `startup.sh` builds gen_data headless, shards across all cores, uploads to a
      GCS bucket, self-deletes) + `GCP_GUIA_PASSO_A_PASSO.md` (PT, beginner). Train
      locally on the RTX 4050. Gotchas learned & fixed: 32-vCPU new-account quota
      (use c2d-standard-32), `--scopes=storage-rw,compute-rw` (self-delete needs
      compute), no `set -e`+`tee` (SIGPIPE killed a run). ~13M positions ≈ <$1.
- [x] **NNUE inference SIMD** — DONE. AVX2 accumulator add/remove + Lizard SCReLU
      forward in `nnue.cpp`, bit-exact vs scalar (verified by node counts +
      `accumulator_matches_refresh`). NPS 471k→583k. Scalar fallback kept for
      non-AVX2 builds. Further headroom: int8 output layer / `maddubs` like SF.
- [ ] **SEE in move ordering** — currently SEE is only a quiescence filter. Order
      captures by SEE (good/equal above killers, losing captures last) and add
      SEE-based pruning of losing captures at low depth in the main search.
      Watch cost: SEE per move at every node is expensive — measure NPS.
- [ ] **Eval tuning (Texel)** — tune the hand-crafted term weights. Lower priority
      now that NNUE is the default/stronger eval; only worth it to strengthen the
      secondary HCE option.
- [x] **Singular extensions — DONE** (`main`). At `depth>=10`, re-search the node
      excluding the TT move at `(depth-1)/2` under a `3*depth` margin; if every
      other move fails low, extend the TT move +1 ply. SPRT **+26 fixed-nodes /
      +31 wall-clock**. LESSON: the untuned first cut (`depth>=8`, `2*depth`) was
      **−27 Elo** — the verification fired too often and ate the node budget.
      Tightening the trigger flipped it. Don't loosen the trigger without re-SPRT.
- [ ] **More search heuristics** — internal iterative reductions, history
      gravity/aging, double/negative singular extensions (extend +2 when very
      singular; reduce when `singularBeta>=beta` = multicut). Each small; SPRT.
      NOTE: improving-flag + continuation-history + history-based LMR were tried
      together and regressed (reverted) — re-attempt individually, tuned, with SPRT.

## Speed (NPS / depth)
- [x] **Captures-only quiescence — DONE** (`main`). Quiescence (most nodes) only
      ever plays captures/promotions but used to generate + sort ALL legal moves
      then skip the quiets. `generate_legal_captures` (same legal filter, narrower
      pseudo list, same relative order = bit-identical search) cut ~12% wall-clock
      at fixed depth → SPRT **+30 Elo / LOS 99%**. (LTO was tried alongside and
      measured neutral with `-O3 -march=native` already on — not merged.)
- [x] **Direct legal move generation — DONE** (`movegen.cpp`, on `main`). checkers +
      pinned filter, no make/unmake per move (en-passant verified once). ~2x NPS
      (NNUE 583k→1325k), **+163 Elo** SPRT. Validated on 5 CPW perft positions.
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
- [x] **Mate/stalemate detection** + **Evaluation menu** (HCE/NNUE) + **new-game
      freeze fixed** (game-id instead of a discard counter) — all done.
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
