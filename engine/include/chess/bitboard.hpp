#pragma once
// =============================================================================
// chess/bitboard.hpp - operations on a 64-bit board (one bit per square, LERF).
//
// A Bitboard is a SET of squares: bit i set  <=>  square i is occupied.
// Because square = rank*8 + file (LERF), whole-board steps are just shifts:
//   north <<8, south >>8, east <<1, west >>1.
// The catch for east/west and diagonals is FILE WRAP: shifting the h-file east
// would spill onto the a-file of the next rank. We mask the edge file off first.
// =============================================================================

#include <bit>            // std::popcount, std::countr_zero - constexpr in C++20
#include "chess/types.hpp"

namespace chess {

// -----------------------------------------------------------------------------
// File / rank masks (given - these are conventions, like the enums in types.hpp)
//   FILE_A_BB lights a1,a2,...,a8 = bits 0,8,16,...,56 = 0x0101010101010101
// -----------------------------------------------------------------------------
constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;            // 0x8080808080808080
constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;     // a1..h1
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;           // a8..h8

// -----------------------------------------------------------------------------
// Single-square helpers
// -----------------------------------------------------------------------------

// One bit set, for square s.   square_bb(SQ_E4) = 1<<28 = 0x0000000010000000
constexpr Bitboard square_bb(Square s) {
    return Bitboard(1) << s;
}

// Is square s present in b?   nonzero iff the bit is set:  b & square_bb(s)
constexpr bool test(Bitboard b, Square s) {
    return b & square_bb(s);
}

// Add square s to the set.   b |= square_bb(s)
constexpr void set(Bitboard& b, Square s) {
    b |= square_bb(s);
}

// Remove square s from the set.   b &= ~square_bb(s)
constexpr void clear(Bitboard& b, Square s) {
    b &= ~square_bb(s);
}

// -----------------------------------------------------------------------------
// Counting & scanning
// -----------------------------------------------------------------------------

// How many squares are in the set (how many pieces).   popcount(0xFF) = 8
constexpr int popcount(Bitboard b) {
    return std::popcount(b);
}

// Lowest set square = trailing-zero count. Precondition: b != 0.
//   lsb(square_bb(SQ_C2)) = SQ_C2
constexpr Square lsb(Bitboard b) {
    return static_cast<Square>(std::countr_zero(b));
}

// Return the lowest set square AND clear it from b. This is the move-gen
// iterator: `while (bb) Square s = pop_lsb(bb);`
// Clear-lowest-set-bit trick:  b &= b - 1
//   b      = 0b1011'0100
//   b - 1  = 0b1011'0011
//   b&(b-1)= 0b1011'0000   (the lowest set bit, bit 2, is gone)
constexpr Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}

// -----------------------------------------------------------------------------
// Directional shifts (mind the file wrap)
// -----------------------------------------------------------------------------

// north/south never wrap: bits shifted past the edge just disappear.
constexpr Bitboard north(Bitboard b) { return b << 8; }
constexpr Bitboard south(Bitboard b) { return b >> 8; }

// east/west must drop the edge file BEFORE shifting, or h wraps to a (and back).
constexpr Bitboard east(Bitboard b) { return (b & ~FILE_H_BB) << 1; }
constexpr Bitboard west(Bitboard b) { return (b & ~FILE_A_BB) >> 1; }

// Diagonals = one rank + one file step, so they mask the same edge file as the
// horizontal step they contain:
//   north_east <<9 (drop H)   north_west <<7 (drop A)
//   south_east >>7 (drop H)   south_west >>9 (drop A)
constexpr Bitboard north_east(Bitboard b) { return (b & ~FILE_H_BB) << 9; }
constexpr Bitboard north_west(Bitboard b) { return (b & ~FILE_A_BB) << 7; }
constexpr Bitboard south_east(Bitboard b) { return (b & ~FILE_H_BB) >> 7; }
constexpr Bitboard south_west(Bitboard b) { return (b & ~FILE_A_BB) >> 9; }

} // namespace chess
