#pragma once
// =============================================================================
// chess/nnue.hpp - NNUE evaluation interface (see docs/NNUE.md for the full plan).
//
// Phase 0 (this file): the feature indexing (implemented + testable) and the
// public interface. The accumulator is PER-POSITION state (lives on each thread's
// Position copy); the network WEIGHTS are shared, read-only (loaded once). So this
// drops into the Lazy-SMP architecture with no concurrency reasoning - exactly the
// seam we built the search around.
//
// "NNUE-lite" first target (Phase 1-3):
//   768 sparse features -> L1=256 accumulator (per perspective) -> 512->32->32->1
// Upgrade to HalfKA (king-bucketed) in Phase 4.
// =============================================================================

#include <cstdint>
#include <string>

#include "chess/types.hpp"

namespace chess {

class Position;   // forward declaration (interface only needs a reference)

namespace nnue {

// ---- Network shape (NNUE-lite) ----------------------------------------------
constexpr int COLORS      = 2;
constexpr int PIECE_KINDS = 6;                       // PAWN..KING
constexpr int SQUARES     = 64;
constexpr int INPUT_DIM   = COLORS * PIECE_KINDS * SQUARES;  // 768
constexpr int L1          = 256;                     // accumulator size per perspective

// ---- Feature indexing (pure, implemented now) -------------------------------
// The index of the "this piece is on this square" feature, as seen from one
// side's PERSPECTIVE. From the perspective side, the board is oriented so that
// "my" back rank is rank 1: White looks at the real board, Black at the board
// mirrored vertically (sq ^ 56). Own pieces occupy bucket 0, enemy pieces bucket
// 1. This makes the two perspectives symmetric, so the net learns one notion of
// "my pieces vs your pieces". `pt` is a PieceType in [PAWN..KING].
constexpr int feature_index(Color perspective, Color pieceColor, PieceType pt, Square sq) {
    int orientedSq  = (perspective == WHITE) ? int(sq) : (int(sq) ^ 56);
    int colorBucket = (pieceColor == perspective) ? 0 : 1;     // 0 = own, 1 = enemy
    return colorBucket * (PIECE_KINDS * SQUARES) + (int(pt) - 1) * SQUARES + orientedSq;
}

// ---- Accumulator: PER-POSITION hidden state ---------------------------------
// v[perspective][neuron]. Kept incrementally in make/unmake (Phase 2), mirroring
// the incremental Zobrist key. Each thread's Position owns one - never shared.
struct alignas(32) Accumulator {   // 32-byte aligned for AVX2 loads/stores
    alignas(32) std::int16_t v[COLORS][L1] = {};
    bool         valid = false;   // false => must be refreshed from scratch
};

// ---- Public interface (implemented in engine/src/eval/nnue.cpp, Phase 1) -----

// Load a network file; returns false (and leaves NNUE disabled) if absent/invalid.
// Wire to a UCI `EvalFile` option. While unloaded, evaluate() must fall back to HCE.
bool load(const std::string& path);
bool is_loaded();
void unload();   // drop the loaded net (evaluate() falls back to HCE)

// Load the network compiled into the binary (tools/embed_net.py -> embedded_net.cpp).
// Lets the engine use NNUE with no external file. Returns false if no net is embedded.
bool load_embedded();

// Recompute the accumulator from scratch for `pos` (the from-scratch reference,
// and the Phase-2 correctness gate: incremental updates must always equal this).
void refresh(Accumulator& acc, const Position& pos);

// Run the quantized forward pass from a (valid) accumulator and return the eval in
// centipawns from `stm`'s perspective (same convention as the HCE evaluate()).
int forward(const Accumulator& acc, Color stm);

// ---- Incremental updates (Phase 2) ------------------------------------------
// Called from Position::put_piece / remove_piece so the accumulator tracks the
// board exactly, the way the Zobrist key does. Each adds/subtracts one feature's
// column for both perspectives - a handful of int16 adds, no matmul. Because
// make/unmake call put/remove symmetrically, the updates auto-revert on unmake.
// Safe to call only on a `valid` accumulator with a loaded net.
void add_piece(Accumulator& acc, Color c, PieceType pt, Square sq);
void remove_piece(Accumulator& acc, Color c, PieceType pt, Square sq);

// Debug correctness gate: does the (incrementally maintained) accumulator equal a
// from-scratch refresh of the same position? Must hold after every make/unmake.
bool accumulator_matches_refresh(const Accumulator& acc, const Position& pos);

// Test/bootstrap helper: install a small deterministic in-memory network (so the
// incremental==refresh gate can run without a trained net file). Not for play.
void make_random_net(unsigned seed);

} // namespace nnue
} // namespace chess
