# NNUE — design & implementation guide (for future sessions)

This is the plan for replacing the hand-crafted evaluation (HCE) with an **NNUE**
(Efficiently Updatable Neural Network). It is the single biggest strength jump
available to a classical alpha-beta engine: plausibly **+300–600 Elo** over our
HCE. Read `CLAUDE.md` first (build path gotcha, "user codes along", SPRT harness).

> Status: **NNUE IS THE DEFAULT EVAL — merged to `main`, beats HCE on the clock.**
> The journey (fixed-nodes SPRT vs HCE unless noted):
>   - 355k positions          -> -720 Elo   : total collapse (too little data)
>   - 5.04M @5000n self-play   -> -99 Elo    : plays real chess, just below HCE
>   - 13.4M @8000n self-play   -> +108 Elo   : first net to beat HCE (fixed nodes)
>   - **net_pub: public 37GB Leela/SF binpack (nodes5000pv2_UHO, ODbL)** ->
>     **+240 Elo vs the 13.4M net, and +237 vs HCE at WALL-CLOCK 8+0.08** (LOS 100%).
> **Data quality/volume was the whole story.** net_pub is embedded in the binary
> (`tools/embed_net.py` -> `embedded_net.cpp`) and loaded at startup; UCI `Eval`
> defaults to NNUE. Source net kept at `C:\chess_sprt\data\net_pub.nnue`
> (our NN01 format, 768->256x2->1).
>
> Next levers: (a) **even more / bigger public data** (the 16B `data_d9` binpack);
> (b) **bigger net / HalfKA king-buckets** (the next quality jump; needs int8 for
> speed); (c) **int8 + maddubs** forward for more nodes/sec; (d) **bootstrapping is
> now valid** - label new data with net_pub (search guided by it beats HCE). The
> wall-clock win already stands because the legal-movegen rewrite (+163 Elo, on
> `main`) roughly doubled NPS, paying for NNUE's eval cost. See docs/TODO.md.

### Speed: eval-quality win vs eval-cost (the wall-clock story)
The +108 is at FIXED NODES (isolates eval quality). NNUE costs more per eval, so at
a real time control it searches shallower; net Elo = quality gain - speed loss.
- scalar NNUE: ~471k nps (HCE ~800k) -> at 8+0.08s NNUE was **-134 Elo** vs HCE.
- **SIMD it.** The net is embedded (tools/embed_net.py) + selectable via UCI `Eval`
  (HCE default / NNUE) and the GUI Evaluation menu. AVX2 work in `nnue.cpp`:
  - accumulator add/remove: `_mm256_add/sub_epi16`, 16 int16/instr; Accumulator is
    `alignas(32)`.
  - forward: **Lizard SCReLU trick** - reorder `(v*v)*w` to `v*(v*w)`, compute
    `v*w` in int16 (255*127 < 32767), then `_mm256_madd_epi16(v, v*w)` sums pairs to
    int32 in one instruction (no unpack/widen).
  - bit-exact vs scalar (same eval, same node counts); `accumulator_matches_refresh`
    gate still passes.
  - NPS: 471k -> 536k (pass 1) -> 583k (Lizard).
- **Wall-clock verdict (clean run, idle machine, concurrency 4, 8+0.08, 200 games):
  NNUE -33 Elo vs HCE** (LOS ~5%). So SIMD cut the real-time gap from -134 to -33,
  but NNUE is still ~30% slower per eval and the lost depth outweighs the +108
  quality at this hardware/TC. (The earlier -4 / -75 readings were CPU-contention
  noise; -33 is the trustworthy figure.)
- **DECISION: HCE stays the default** - it's the stronger engine on the clock today.
  NNUE remains selectable (UCI `Eval` / GUI) and is the better *evaluation* (+108 at
  fixed nodes); it just isn't fast enough yet to win real games.
- To make NNUE the default later, close the speed gap further: int8 output layer +
  `_mm256_maddubs_epi16` (like Stockfish), or a smaller/faster L1, or only-eval-when-
  needed in search. Re-run the clean wall-clock SPRT; flip the default if NNUE wins.

### Bootstrapping is PREMATURE until NNUE > HCE (tested, -79 Elo)
We tried the AlphaZero-style loop early: generated 3M positions LABELED BY net5m
(via `gen_data <...> <net>`), trained v2, SPRT v2 vs v1(net5m) at 20k nodes =>
**v2 was -79.5 Elo WORSE** (400 games, H0). Why: bootstrapping only helps when the
teacher (search guided by the current net) produces *better* labels than what you
had. But net5m is still weaker than the HCE, so labeling with it is learning from
a worse teacher than the HCE we already use - it amplifies the net's weaknesses.
**Rule: keep labeling with the HCE (the best evaluator we have) until a net beats
the HCE in SPRT. Only then does net-labeled bootstrapping start to pay off.** The
`-Net`/`[evalfile]` machinery is correct and ready; just don't use it yet.

## Hard-won lessons (read before touching NNUE again)

1. **Architecture matches `bullet`'s `simple.rs` exactly** so we load its raw
   quantised `.bin` with no exporter: `(768->256)x2->1`, **SCReLU**, `QA=255`,
   `QB=64`, `SCALE=400`. `nnue.cpp` reads `[ftW, ftB, outW, outB]` (all i16) and
   ignores bullet's 64-byte trailing pad. Our `feature_index` already equals
   bullet's `Chess768` mapping (own/enemy bucket, black mirrors `sq^56`).

2. **The mechanics are correct and verified** - do not re-debug these:
   - eval sign is side-to-move-relative (checked KQvK and full-board, both stms);
   - the incremental accumulator equals a from-scratch refresh across a 120-ply
     game AND across null moves (verified with a scratch tool);
   - material is learned (a side up a queen reads ~+2000cp, symmetric).

3. **355k positions is far too little.** The first net opened fine but chose weak
   positional moves in balanced middlegames and got mated (SPRT: 1.5%, -720 Elo).
   The eval was *honest* (its score fell as it worsened) - it just hadn't learned
   nuance. The fix is DATA, not code. Target millions (this run: ~4.8M).

4. **Validate by PLAYING, not by eyeballing static eval.** Material/symmetry spot
   checks all passed on the losing net; only real games (the SPRT) exposed it.
   When testing the next net, also run a few games and watch a PGN, not just evals.

5. **NNUE inference is ~3x slower than HCE** (scalar 256-wide forward). For a fair
   "did more data help?" verdict, run the SPRT at **fixed nodes/depth** (e.g.
   `option.Nodes` or a depth limit) so net *quality* is isolated from inference
   *speed*. SIMD-ing `forward()` (AVX2 over the i16 accumulator) is the perf fix,
   but it's a later, carefully-verified step - a wrong SIMD pass silently corrupts
   eval, the exact bug class that cost an hour here.

## Re-train workflow (data already generated)

`powershell -File tools\training\retrain.ps1` does: normalize -> convert to
bulletformat -> train (bullet+CUDA, ~seconds on the GPU). Then copy the newest
`checkpoints\chessengine-*\quantised.bin` to `C:\chess_sprt\data\net.nnue` and
SPRT vs HCE (ideally fixed-nodes; see lesson 5).

## Why NNUE, and the paradigm note

- Stockfish has used NNUE since **SF12 (2020)**; the classical eval was **deleted
  in SF16 (2023)**. NNUE originated in **Shogi** (Yu Nasu, 2018), ported to chess
  by *nodchip*. "Pulling Stockfish's modern eval" = NNUE.
- **It stays CPU-only and still alpha-beta.** NNUE inference is integer SIMD on the
  CPU; only *training the net* uses a GPU (offline). This does NOT contradict the
  project's no-GPU-for-search stance (CLAUDE.md): the engine never needs a GPU.
- This session showed *why* it matters: hand-crafted terms are finicky (king
  safety v1 lost 16 Elo from bad scaling; v2 tuned was neutral). NNUE **learns**
  all those terms from data instead of us calibrating each by hand.

## How it fits our architecture (this is the whole point)

We already split shared vs per-thread state for Lazy SMP (`search.cpp`). NNUE drops
into the same seam with **no concurrency reasoning**:

| Piece | Lives in | Shared? |
|---|---|---|
| **Network weights** (the `.nnue` file) | inject via `SharedState` (like the TT) | **shared, read-only** |
| **Accumulator** (per-position hidden state) | inside `Position` (or the search stack) | **per-thread** (each Worker has its own `Position`) |
| `evaluate(pos)` | unchanged public interface | — |

So: load weights once, share by const-reference; each thread mutates only its own
accumulator. `evaluate()` stays the façade — today it sums PSQT+mobility; with
NNUE it runs the forward pass from the accumulator.

## The network (start simple, then grow)

Two-stage plan. **Phase 1–3 build "NNUE-lite"; Phase 4 upgrades to HalfKA.**

### NNUE-lite (first target)
```
features (768, sparse, ~32 on) ──▶ Accumulator (perspective×L1)
                                     L1 = 256, one linear layer (the "feature transformer")
Accumulator (2×256 = 512) ──▶ 512→32 ──▶ 32→32 ──▶ 32→1  ──▶ eval (cp-ish)
                              clipped-ReLU between layers
```
- **Feature set (768):** index = `pieceColor*384 + (pieceType-1)*64 + square`. 2 colors
  × 6 piece types × 64 squares = 768. Only ~32 features are "on" (one per piece).
- **Two perspectives:** compute the accumulator once from the side-to-move's view
  and once from the opponent's (board mirrored vertically: `sq ^ 56`), concatenate.
  The net learns "my pieces vs your pieces" symmetrically.
- **L1 = 256** hidden neurons (tunable; 128 is faster/weaker, 256 is a good start).

### HalfKA (Phase 4, the strong version)
- Feature = `(our king square, piece, piece square)` → ~`64 × (64×11)` inputs, only
  the ~32 active. King-bucketing is what gives NNUE its real strength (eval depends
  on where *our* king is). Much bigger first layer; same machinery otherwise.

## The "Efficiently Updatable" trick (the core insight)

The first layer (768→256) would be expensive to recompute every node. But a move
toggles only a few features (piece leaves a square, lands on another; captures
remove one). So keep the accumulator **incrementally**, updated in
`make_move`/`unmake_move` — *exactly like the incremental Zobrist key we already
maintain*:

```
on add_piece(c, pt, sq):  acc[persp] += W.column[feature_index(persp, c, pt, sq)]   (both perspectives)
on remove_piece(...):     acc[persp] -= W.column[...]
```
A move = a couple of removes + adds = a few hundred int16 adds (SIMD), not a matmul.

**Correctness gate (the "perft" of NNUE):** the incrementally-updated accumulator
must *always* equal a from-scratch refresh of the same position. Add a debug assert
(`accumulator_matches_refresh(pos)`) and check it across a perft walk before trusting
incremental updates. This is the #1 source of NNUE bugs.

## Quantization (why it's fast)

- Feature-transformer weights/accumulator: **int16**. Output-layer weights: **int8**;
  biases int32. Activation = **clipped ReLU** to `[0, 127]`.
- All integer arithmetic → AVX2/SSE friendly. Start with a **plain scalar** forward
  pass (correct first), then add SIMD once it works.
- Watch for **overflow**: int16 accumulator must not saturate; pick weight scale
  accordingly (the trainer enforces this — typically scale 127 for activations,
  64 for weights).

## File format

Define our **own simple format** (don't try to parse Stockfish's — it changes and
is GPL). Suggested little-endian layout:
```
magic "NNUE" | uint32 version | uint32 L1 | int16 ft_weights[768*L1] | int16 ft_bias[L1]
            | int8 l1_w[...] | int32 l1_b[...] | ... | int8 out_w[...] | int32 out_b
```
Keep a `nnue::load(path)` and a UCI `EvalFile` option (default: HCE if absent).

## Training (offline, GPU)

- Use **`bullet`** (Rust, the modern standard, https://github.com/jw1912/bullet) or
  **nnue-pytorch**. Output a net in *our* format (write a tiny exporter).
- **Data:** millions of positions labelled by a score + game result. Cheapest source:
  **self-play with our own engine** at fixed nodes/depth (the SPRT harness can
  generate games; dump FEN + search score + result). Or start from a public dataset
  (e.g. the Stockfish "data" lc0/leela-T80 sets are large; check licence).
- **Loss:** blend of (search eval) and (game result), the standard NNUE recipe.
- Training is GPU and offline; the produced `.nnue` is what the engine loads.

## Pretrained net vs train-our-own (decision)

Three options, and why we picked one:

1. **Reuse a Stockfish net** — they are **GPLv3**, so embedding one relicenses our
   whole engine as GPL. Also not plug-and-play: their topology (HalfKAv2_hm, sizes,
   quantization) is specific, so we'd have to match our inference to *their* format
   byte-for-byte. Rejected (licence + coupling).
2. **Reuse a permissively-licensed net** (MIT/Apache, smaller projects) — licence is
   fine, but these are almost always a *different topology*, so "use it" really means
   "implement that architecture's inference and load its weights". Not free.
3. **Train our own** — clean licence, and the net matches our own feature scheme.
   Cheap: self-play data + `bullet` on a modest GPU trains a small net in hours.

**Decision: train our own (option 3), and define our OWN file format** (do not parse
SF's `.nnue`). This keeps Phase 1 simple and the licence clean. The inference code is
the same regardless of where weights come from, so this choice only affects training.

Bootstrap shortcut for the *first* net: label positions with **our current HCE +
shallow search** (distill what we already have) before moving to real self-play
labels. Gets a working net fast, then iterate.

## License

Stockfish nets and code are **GPLv3** — copying a net or NNUE code relicenses our
engine GPL. To stay permissive: **train our own net** (clean-room, see decision
above), and write our own inference. The *concepts* here are not encumbered; specific
SF weights are.

## Phased checklist

- [x] **Phase 0** — this doc + `nnue.hpp` scaffold (feature index, structs, contracts).
- [ ] **Phase 1 — inference skeleton (no real net):**
      - `feature_index()` (done in scaffold) + `refresh_accumulator(pos)` (from scratch).
      - `forward(acc)` scalar quantized pass → returns a cp score.
      - `nnue::load(path)`; UCI `EvalFile` option; `evaluate()` uses NNUE iff a net
        is loaded, else HCE. Validate with a **zero/random net** (runs, returns a number).
- [ ] **Phase 2 — incremental accumulator:**
      - Hook add/remove piece in `make_move`/`unmake_move` to update `Position`'s
        accumulator. Add `accumulator_matches_refresh()` assert; verify across perft.
- [ ] **Phase 3 — train a net:**
      - Generate self-play data (FEN, score, result) via the harness.
      - Train `768→256→32→32→1` with `bullet`; export to our format.
      - **SPRT NNUE vs HCE.** Expect a large gain; iterate on net size/data.
- [ ] **Phase 4 — HalfKA upgrade + SIMD:**
      - King-bucketed features; AVX2 forward pass; bigger/refined net. SPRT each step.

## Gotchas (don't regress)

- Build in an **ASCII path** (`C:/chess_build`) — Qt moc + non-ASCII (CLAUDE.md).
- **Perspective/mirror:** black's view mirrors the board (`sq ^ 56`) and swaps piece
  colours; get this exactly right or eval is asymmetric. Test: `eval(pos) == -eval(mirror(pos))`.
- **Incremental == refresh** must hold after every make/unmake (the correctness gate).
- Each Worker needs its **own** accumulator (it's in `Position`, which Workers copy) —
  never share it between threads. Weights are shared read-only.
- Keep `evaluate()` returning **side-to-move-relative** centipawns (as today), so the
  rest of the search is unchanged.
