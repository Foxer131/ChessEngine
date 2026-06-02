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
iterative deepening, aspiration, shared lockless TT, **captures-only quiescence**
+ SEE, move ordering with SEE-split captures, null move, RFP/futility/LMP,
log-LMR, check extensions, **singular extensions**), **Lazy SMP** (UCI `Threads`,
+127 Elo 8v1), **opening book**, **draw + mate/stalemate detection** (engine +
GUI), Qt GUI (legal-move-enforcing, eval selector). The NNUE training +
cloud-datagen pipeline is built. Strength: well above the old HCE-only engine.

**Recent SPRT-validated wins (all on `main`):** captures-only quiescence (+30),
singular extensions (+26 fixed-nodes / +31 wall-clock). Both passed the SPRT
after an "obvious"-looking false start (LTO measured neutral and was dropped;
singular's untuned v1 was −27 before the trigger was tightened). The discipline
holds: **no strength change ships without an SPRT** — eyeball impressions (incl.
a gorgeous drawn game vs the Magnus bot) have been wrong here repeatedly.

## Strength
- [x] **NNUE evaluation — DONE and is the DEFAULT** (on `main`, see docs/NNUE.md).
      Full pipeline: self-play datagen (`gen_data`), cloud generation
      (`tools/cloud`), `bullet` training, our NN01 inference with incremental AVX2
      accumulator, embedded in the binary (`tools/embed_net.py`), UCI `Eval` + GUI
      switch. The current net (`net_pub`) is trained on **public Leela/SF data**
      (ODbL) and beats HCE **~+237 Elo wall-clock** (+350 fixed-nodes); public data
      beat our 13.4M self-play net by +240. Data quality/volume was the lever.
- [ ] **NNUE next — START with the data-saturation test** (cheap, decides the path):
        1. Retrain the **same 256-wide net** on **~2× the current public data**.
        2. Fixed-nodes SPRT vs `net_pub`.
        3. **Wins** → not saturated → download the 16B `data_d9` binpack for more.
           **Ties** → saturated → the next lever is a **bigger net (HalfKA
           king-buckets) + int8**, NOT more data.
      Rationale: model capacity and data volume are coupled; dumping data into a
      near-saturated 256 net gives little. Also valid now: **bootstrapping** —
      label our cloud-datagen positions with `net_pub` (it beats HCE) for quality,
      not just volume (`gen_data <...> <net>` / `gen_shards -Net`). Heavy data prep
      is CPU-bound — never run it during an SPRT.
- [ ] **NNUE bigger net + int8** (the jump after data saturates): HalfKA wants
      int8; **int8 + `_mm256_maddubs_epi16`** forward also buys nodes/sec (forward
      dominates eval cost), which directly funds a more expensive net on the clock.
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
- [x] **Singular extensions — DONE** (`main`). At `depth>=10`, re-search the node
      excluding the TT move at `(depth-1)/2` under a `3*depth` margin; if every
      other move fails low, extend the TT move +1 ply. SPRT **+26 fixed-nodes /
      +31 wall-clock**. LESSON: the untuned first cut (`depth>=8`, `2*depth`) was
      **−27 Elo** — the verification fired too often and ate the node budget.
      Tightening the trigger flipped it. Don't loosen the trigger without re-SPRT.
- [x] **SEE-split move ordering — DONE** (`score_move` in `search.cpp`). Good/equal
      captures sort above killers (1M+MVV-LVA), losing-by-SEE captures sort last
      (−1M). What REMAINS (the open part): **SEE-pruning of losing captures at low
      depth in the *main* search** (today SEE only filters them in quiescence).
      Small, SPRT it; watch the per-node SEE cost.
- [ ] **More search heuristics** — internal iterative reductions, history
      gravity/aging, double/negative singular extensions (extend +2 when very
      singular; treat `singularBeta>=beta` as multicut → return early). Each small;
      SPRT. NOTE: improving-flag + continuation-history + history-based LMR were
      tried together and regressed (reverted) — re-attempt individually, tuned.
- [ ] **Correction history / eval blending** — small learned correction to the
      eval from recent search results; cheap, modern, measurable. SPRT.
- [ ] **Eval tuning (Texel)** — tune the hand-crafted term weights. Low priority
      now that NNUE is the default/stronger eval; only worth it to strengthen the
      secondary HCE option (e.g. an aggressive "personality" HCE — see below).

## Speed (NPS / depth)
- [x] **Captures-only quiescence — DONE** (`main`). Quiescence (most nodes) only
      ever plays captures/promotions but used to generate + sort ALL legal moves
      then skip the quiets. `generate_legal_captures` (same legal filter, narrower
      pseudo list, same relative order = bit-identical search) cut ~12% wall-clock
      at fixed depth → SPRT **+30 Elo / LOS 99%**.
- [x] **Direct legal move generation — DONE** (`movegen.cpp`, on `main`). checkers +
      pinned filter, no make/unmake per move (en-passant verified once). ~2x NPS
      (NNUE 583k→1325k), **+163 Elo** SPRT. Validated on 5 CPW perft positions.
- [x] **Lazy SMP (multithreading)** — DONE, **on `main`**. N `Worker`s search the
      same root sharing only a `TranspositionTable` (lockless: key-validated, torn
      writes tolerated). Each Worker owns its Position copy + history/killers/
      counters + repetition list. Shared things are injected by reference via
      `SharedState` (constructor injection) — so **adding a new heuristic is a
      Worker field, never a concurrency problem**. UCI option `Threads`; the GUI
      sets it to cores-1. Threads=1 is bit-identical to single-threaded. *Future
      search heuristics should keep landing on `Worker` so they stay thread-agnostic.*
- [ ] **Lazy eval** (Stockfish trick): skip the NNUE forward when a cheap estimate
      is already far outside `[alpha,beta]`. Cuts per-node cost in decided
      positions; helps the bigger-net path most. SPRT.
- Tried & REJECTED: **LTO/IPO** measured neutral (~0%) with `-O3 -march=native`
  already on (few translation units, GCC already inlines within them). Branch
  `perf/lto` kept for the record; don't re-enable expecting a gain.

## Features
- [ ] **Proper time management — the real fix for "minutes per move"** (highest
      feature-value item). Today the engine only honors a flat `movetime`/`nodes`/
      `depth`; it does NOT read `wtime/btime/winc/binc/movestogo`. Fixed-depth in
      the GUI has *no clock*, so a dense position at depth 17 genuinely costs
      tens of seconds — that is the cause of the slowness reports, NOT a bug and
      NOT weak pruning (measured EBF is a healthy ~1.7–1.9). Implement soft/hard
      limits from the clock and don't start an iteration that can't finish
      (predict from branching factor). `SearchLimits` is in `search.hpp`.
- [ ] **Polyglot `.bin` book support** — to use downloadable books. Requires
      embedding Polyglot's exact 781-entry Zobrist array (verify against the
      published start-position key `0x463b96181691fc9c`) and decoding Polyglot's
      move encoding. Our current book uses our own Zobrist (no `.bin` compat).
- [ ] **Insufficient-material draws** (KvK, KvKN, KvKB, KNvK) in engine + GUI.
- [ ] **Aggressive "Tal" personality (HCE)** — optional, fun, NOT max strength.
      King tropism + open-lines-to-king + initiative bonuses + contempt
      (anti-draw) on the *hand-tunable HCE* (the NNUE is a style-neutral black
      box). Validate by watching PGNs, not SPRT (it trades Elo for style).
- [ ] **Pondering** (search on the opponent's clock) — optional.
- [ ] **Syzygy endgame tablebases** — optional, large dependency.

## GUI (Qt)
- [x] **Mate/stalemate detection** + **Evaluation menu** (HCE/NNUE) + **new-game
      freeze fixed** (game-id instead of a discard counter) — all done.
- [x] **SVG piece sprites — DONE**. Replaced the Unicode-glyph rendering with the
      **cburnett** SVG set (open BSD/GPL/GFDL licence, used under BSD — see
      `gui/resources/pieces/CREDITS.md`; NOT chess.com's proprietary art).
      Rendered via `QSvgRenderer` (cached per piece), embedded with a `.qrc`
      (AUTORCC). Needs the **`mingw-w64-x86_64-qt6-svg`** module (now a build dep;
      `find_package(Qt6 COMPONENTS Widgets Svg)`). Glyph rendering kept as a
      fallback if a sprite is missing.
- [x] **Highlight legal moves — DONE**. `GuiBoard::legalDestinations(file,rank)`
      (filters `generate_legal`); `BoardWidget` paints a centred dot on empty
      targets and a ring on captures for the selected piece.
- [ ] Move list / PGN pane, a clock, board-flip toggle, takeback/undo,
      load/save FEN & PGN, a result banner.
- [ ] Show engine score/PV more richly (currently a raw `info` line in the log).

## Infra / build / distribution
- [x] **SPRT testing harness** — DONE. `tools/` (build_versions.ps1, sprt.ps1) and
      `tools/training/sprt_nnue.ps1`, using **fastchess**. Stages binaries+book to
      an ASCII path, puts MSYS2 on PATH. `sprt.ps1` does wall-clock; for fixed
      nodes pass `nodes=N` in fastchess `-each` (sprt_nnue.ps1 has `-Nodes`, or run
      fastchess directly — see `C:\chess_sprt\run_singular` for the last invocation).
      USE IT for every strength change. Fixed-nodes isolates eval/idea quality from
      speed; wall-clock is the real-play verdict (run both for search heuristics).
- [ ] **`windeployqt`** to bundle Qt DLLs next to `chess_gui.exe` so it runs
      without MSYS2 on PATH (and consider moving the project to an ASCII path so
      CMake presets work and the moc gotcha disappears — see CLAUDE.md).
- [ ] Expand perft suite (more reference positions) and consider a unit-test
      framework (doctest) if `core_tests.cpp` grows large.
- [ ] CI (GitHub Actions) building + running tests, if the project goes remote.

## Lower-priority strength ideas (each: implement small, SPRT, keep only if it gains)
- [ ] **Opening book from real games / bigger book**: our book is tiny. A broader,
      vetted book avoids weak openings (the eval can't fix a bad opening).
- [ ] **Aspiration window tuning**: the widen-failing-bound logic is in; the
      initial window size and re-search policy are untuned knobs.

## Known gotchas (don't regress)
- Build in an **ASCII path** (`C:/chess_build`) — Qt `moc` fails on the accented
  source path. New CMake *targets* (from `if(EXISTS ...)`) need a reconfigure.
- **A great-looking game is not evidence of strength.** Singular v1 drew the
  Magnus bot beautifully and measured −27 Elo. Always SPRT.
- The first stdin line a fresh engine process receives can be dropped by some
  test harnesses (PowerShell pipes / first `WriteLine`); real GUIs send `uci`
  first so it's harmless. Don't "fix" this in the engine.
- Headers under `include/chess/` are definitions only; all tests live in
  `tests/core_tests.cpp`. Don't reintroduce per-header `*_check.cpp`.
