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

// =============================================================================
// Compile-time tests. Zero runtime cost; a firing static_assert names the bug.
// =============================================================================
static_assert(square_bb(SQ_A1) == 1ULL,        "a1 is bit 0");
static_assert(square_bb(SQ_H8) == (1ULL << 63),"h8 is bit 63");
static_assert(square_bb(SQ_E4) == (1ULL << 28),"e4 is bit 28");

static_assert(test(square_bb(SQ_E4), SQ_E4),  "e4 is set");
static_assert(!test(square_bb(SQ_E4), SQ_D4), "d4 is not");

// set / clear round-trip (they mutate, so drive them in a constexpr function)
constexpr bool set_clear_ok() {
    Bitboard b = 0;
    set(b, SQ_E4);
    bool was_set = test(b, SQ_E4);
    clear(b, SQ_E4);
    return was_set && b == 0;
}
static_assert(set_clear_ok(), "set then clear returns to empty");

static_assert(popcount(0) == 0,            "empty");
static_assert(popcount(RANK_1_BB) == 8,    "a full rank");
static_assert(popcount(~Bitboard(0)) == 64,"full board");

static_assert(lsb(square_bb(SQ_C2)) == SQ_C2, "single-bit lsb");
constexpr int count_by_pop(Bitboard b) { int n = 0; while (b) { (void)pop_lsb(b); ++n; } return n; }
static_assert(count_by_pop(0xF0FULL) == 8, "pop_lsb iterates every bit once");

static_assert(north(square_bb(SQ_E4)) == square_bb(SQ_E5), "");
static_assert(south(square_bb(SQ_E4)) == square_bb(SQ_E3), "");
static_assert(east (square_bb(SQ_E4)) == square_bb(SQ_F4), "");
static_assert(west (square_bb(SQ_E4)) == square_bb(SQ_D4), "");

// the wrap guards: stepping off the edge yields empty, NOT a wrapped square
static_assert(east (square_bb(SQ_H4)) == 0, "h-file east must not wrap to a5");
static_assert(west (square_bb(SQ_A4)) == 0, "a-file west must not wrap to h3");
static_assert(north(square_bb(SQ_E8)) == 0, "off the top is empty");
static_assert(south(square_bb(SQ_E1)) == 0, "off the bottom is empty");

static_assert(north_east(square_bb(SQ_E4)) == square_bb(SQ_F5), "");
static_assert(north_west(square_bb(SQ_E4)) == square_bb(SQ_D5), "");
static_assert(south_east(square_bb(SQ_E4)) == square_bb(SQ_F3), "");
static_assert(south_west(square_bb(SQ_E4)) == square_bb(SQ_D3), "");

} // namespace chess
