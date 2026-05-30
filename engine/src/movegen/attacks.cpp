#include "chess/attacks.hpp"
#include "chess/bitboard.hpp"

namespace chess {
namespace {

// =============================================================================
// The geometry of each non-sliding piece's attacks from one square. These run
// only once (at table init), so clarity beats cleverness.
// =============================================================================

// True iff (f, r) is a real board square. Used to drop off-board jumps.
constexpr bool on_board(int f, int r) {
    return f >= 0 && f < 8 && r >= 0 && r < 8;
}

// A knight on `s` reaches the 8 (+-1,+-2)/(+-2,+-1) offsets that stay on board.
Bitboard compute_knight(Square s) {
    static const int df[8] = {-2, -2, -1, -1, 1, 1, 2, 2};
    static const int dr[8] = {-1,  1, -2,  2, -2, 2, -1, 1};
    const int f = file_of(s), r = rank_of(s);

    Bitboard b = 0;
    
    for (int i = 0; i < 8; ++i)
        if (on_board(f + df[i], r + dr[i]))
            set(b, make_square(File(f + df[i]), Rank(r + dr[i])));
    
    return b;
}

// A king on `s` reaches every adjacent square: (df,dr) in {-1,0,+1}^2 minus (0,0).
Bitboard compute_king(Square s) {
    const int f = file_of(s), r = rank_of(s);
    Bitboard b = 0;
    for (int df = -1; df <= 1; ++df)
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            if (on_board(f + df, r + dr))
                set(b, make_square(File(f + df), Rank(r + dr)));
        }
    return b;
}

// A pawn attacks its two forward diagonals (never straight ahead). The shift
// helpers already drop the edge files, so an a-/h-pawn yields just one diagonal.
Bitboard compute_pawn(Color c, Square s) {
    const Bitboard b = square_bb(s);
    return (c == WHITE) ? (north_east(b) | north_west(b))
                        : (south_east(b) | south_west(b));
}

// -----------------------------------------------------------------------------
// Table storage + one-time fill (provided). The Meyers singleton initializes on
// first access, so callers never need an explicit init step.
// -----------------------------------------------------------------------------
struct Tables {
    Bitboard knight[SQUARE_NB]{};
    Bitboard king[SQUARE_NB]{};
    Bitboard pawn[COLOR_NB][SQUARE_NB]{};

    Tables() {
        
        for (Square s = SQ_A1; s <= SQ_H8; s = Square(s + 1)) {
            knight[s]      = compute_knight(s);
            king[s]        = compute_king(s);
            pawn[WHITE][s] = compute_pawn(WHITE, s);
            pawn[BLACK][s] = compute_pawn(BLACK, s);
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

} // namespace

Bitboard knight_attacks(Square s) { 
    return tables().knight[s]; 
}

Bitboard king_attacks(Square s) { 
    return tables().king[s]; 
}

Bitboard pawn_attacks(Color c, Square s) { 
    return tables().pawn[c][s]; 
}

} // namespace chess
