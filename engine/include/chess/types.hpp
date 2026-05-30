#pragma once
// =============================================================================
// chess/types.hpp - the fundamental type vocabulary for the whole engine.
//
// Header-only, constexpr, dependency-free. Everything (movegen, search, eval)
// includes this. The enums below FIX our conventions; the small helper
// functions near the bottom are yours to implement (look for `// TODO`).
//
// Square convention: LERF (Little-Endian Rank-File)
//   square = rank * 8 + file        a1 = 0, b1 = 1, ... h1 = 7, a2 = 8, ... h8 = 63
//   => file = square & 7,  rank = square >> 3
//   => directional shifts on a Bitboard: north <<8, south >>8, east <<1, west >>1
// =============================================================================

#include <cstdint>

namespace chess {

// One bit per square; bit index == square index (LERF). a1 is bit 0.
using Bitboard = std::uint64_t;

// -----------------------------------------------------------------------------
// Colors
// -----------------------------------------------------------------------------
enum Color : int { 
    WHITE, 
    BLACK, 
    COLOR_NB = 2 
};

// -----------------------------------------------------------------------------
// Piece types. NO_PIECE_TYPE = 0 so the real types occupy 1..6 (handy: they fit
// in 3 bits and leave 0 as "empty").
// -----------------------------------------------------------------------------
enum PieceType : int {
    NO_PIECE_TYPE = 0,
    PAWN = 1, 
    KNIGHT = 2, 
    BISHOP = 3, 
    ROOK = 4, 
    QUEEN = 5, 
    KING = 6,
    PIECE_TYPE_NB = 7
};

// -----------------------------------------------------------------------------
// Colored pieces. Encoding packs color into bit 3 and type into bits 0..2:
//   piece = (color << 3) | type
// So:  type_of(piece) = piece & 7   and   color_of(piece) = piece >> 3
// White pieces are 1..6, black pieces are 9..14, 0 = empty.
// -----------------------------------------------------------------------------
enum Piece : int {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,        //  1..6
    
    B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,        //  9..14
    PIECE_NB = 16
};

// -----------------------------------------------------------------------------
// Castling rights: a 4-bit set, one bit per side+wing. They OR together, so a
// position's rights are the union of the bits below. NO_CASTLING = empty set.
// -----------------------------------------------------------------------------
enum CastlingRights : int {
    NO_CASTLING = 0,
    WHITE_OO    = 1,   // white king-side  (O-O)
    WHITE_OOO   = 2,   // white queen-side (O-O-O)
    BLACK_OO    = 4,   // black king-side
    BLACK_OOO   = 8,   // black queen-side
    ANY_CASTLING = WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO   // 15
};

// -----------------------------------------------------------------------------
// Files (columns a..h) and Ranks (rows 1..8).
// -----------------------------------------------------------------------------
enum File : int {
    FILE_A, 
    FILE_B, 
    FILE_C, 
    FILE_D, 
    FILE_E, 
    FILE_F, 
    FILE_G, 
    FILE_H, 
    FILE_NB = 8
};

enum Rank : int {
    RANK_1, 
    RANK_2, 
    RANK_3, 
    RANK_4, 
    RANK_5, 
    RANK_6, 
    RANK_7, 
    RANK_8, 
    RANK_NB = 8
};

// -----------------------------------------------------------------------------
// Squares, listed in LERF order so the compiler assigns a1=0 .. h8=63 for us.
// -----------------------------------------------------------------------------
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64,
    SQUARE_NB = 64
};

// =============================================================================
// Square <-> file/rank and the packed Piece encoding.
// Plain enums convert to int implicitly but not back, hence the static_casts.
// =============================================================================

// LERF index = rank*8 + file.   e4: rank 3, file 4 -> 0b011'100 = 28
constexpr Square make_square(File f, Rank r) {
    return static_cast<Square>(f + r * 8);
}

// File = low 3 bits.   28 = 0b011'100,  & 0b000'111 -> 0b100 = 4 (FILE_E)
constexpr File file_of(Square s) {
    return static_cast<File>(s & 7);
}

// Rank = high bits.    28 = 0b011'100 >> 3 -> 0b011 = 3 (RANK_4)
constexpr Rank rank_of(Square s) {
    return static_cast<Rank>(s >> 3);
}

// Type = low 3 bits.   B_QUEEN 13 = 0b1101,  & 0b0111 -> 0b101 = 5 (QUEEN)
constexpr PieceType type_of(Piece pc) {
    return static_cast<PieceType>(pc & 7);
}

// Color = bit 3.       B_QUEEN 13 = 0b1101 >> 3 -> 0b1 = 1 (BLACK)
constexpr Color color_of(Piece pc) {
    return static_cast<Color>(pc >> 3);
}

// Pack (color<<3)|type.  BLACK,KING = 0b1000 | 0b0110 = 0b1110 = 14 (B_KING)
constexpr Piece make_piece(Color c, PieceType pt) {
    return static_cast<Piece>(c << 3 | pt);
}

// Flip side: c ^ BLACK.  WHITE 0^1 -> 1 (BLACK) ;  BLACK 1^1 -> 0 (WHITE)
constexpr Color operator~(Color c) {
    return static_cast<Color>(c ^ BLACK);
}

// =============================================================================
// Compile-time tests for the helpers above. These cost nothing at runtime; if
// any ever fires, the named line tells you which conversion broke.
// =============================================================================
static_assert(make_square(FILE_A, RANK_1) == SQ_A1, "a1 must be square 0");
static_assert(make_square(FILE_H, RANK_8) == SQ_H8, "h8 must be square 63");
static_assert(make_square(FILE_E, RANK_4) == SQ_E4, "e4 = 4 + 3*8 = 28");

static_assert(file_of(SQ_E4) == FILE_E, "file of e4 is E");
static_assert(rank_of(SQ_E4) == RANK_4, "rank of e4 is 4");
static_assert(file_of(SQ_H8) == FILE_H && rank_of(SQ_H8) == RANK_8, "");

static_assert(make_piece(WHITE, KNIGHT) == W_KNIGHT, "");
static_assert(make_piece(BLACK, KING)   == B_KING,   "");
static_assert(type_of(B_QUEEN)  == QUEEN, "");
static_assert(color_of(B_QUEEN) == BLACK, "");
static_assert(color_of(W_PAWN)  == WHITE, "");

static_assert(~WHITE == BLACK && ~BLACK == WHITE, "color flip");

} // namespace chess
