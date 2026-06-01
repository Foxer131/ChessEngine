#pragma once
// =============================================================================
// chess/attacks.hpp - attack lookups for the non-sliding pieces.
//
// Knight, king and pawn attacks don't depend on other pieces, so they're
// precomputed once into tables and then just indexed by square (and color for
// pawns). The tables initialize themselves on first use.
//
// (Sliding pieces - bishop/rook/queen - come later via magic bitboards, because
// their attacks DO depend on blockers.)
// =============================================================================

#include "chess/types.hpp"

namespace chess {

Bitboard knight_attacks(Square s);
Bitboard king_attacks(Square s);
Bitboard pawn_attacks(Color c, Square s);

// Sliding pieces: attacks depend on the occupied squares (blockers), resolved
// via magic bitboards. Pass the full board occupancy.
Bitboard bishop_attacks(Square s, Bitboard occupied);
Bitboard rook_attacks(Square s, Bitboard occupied);
Bitboard queen_attacks(Square s, Bitboard occupied);

// For legal move generation. If a and b share a rank/file/diagonal:
//   between_bb(a,b) = the squares strictly between them (empty otherwise).
//   line_bb(a,b)    = the whole line through them (the full rank/file/diagonal).
// Used to find check-blocking squares and to restrict a pinned piece to its pin
// ray. Precomputed 64x64 tables.
Bitboard between_bb(Square a, Square b);
Bitboard line_bb(Square a, Square b);

} // namespace chess
