# NNUE — design & implementation guide (for future sessions)

This is the plan for replacing the hand-crafted evaluation (HCE) with an **NNUE**
(Efficiently Updatable Neural Network). It is the single biggest strength jump
available to a classical alpha-beta engine: plausibly **+300–600 Elo** over our
HCE. Read `CLAUDE.md` first (build path gotcha, "user codes along", SPRT harness).

> Status: **Phase 0 done** (this doc + scaffold in `engine/include/chess/nnue.hpp`).
> The branch is `experiment/nnue`. Pick up at Phase 1.

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
