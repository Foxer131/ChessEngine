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
            if (df == 0 && dr == 0) 
                continue;
            
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

// between/line geometry, computed directly from file/rank deltas (no dependency
// on the magic tables, so init order doesn't matter). If a and b are colinear
// (same rank, file, or diagonal), `between` walks the squares strictly between
// them and `line` is `between | a | b | the two squares just beyond` extended to
// the full board line. We build `line` by walking both directions to the edges.
void compute_between_line(Square a, Square b, Bitboard& between, Bitboard& line) {
    between = 0; line = 0;
    const int fa = file_of(a), ra = rank_of(a);
    const int fb = file_of(b), rb = rank_of(b);
    const int df = fb - fa, dr = rb - ra;
    if (a == b) return;
    // Colinear iff same file (df==0), same rank (dr==0), or diagonal (|df|==|dr|).
    const bool colinear = (df == 0) || (dr == 0) || (df == dr) || (df == -dr);
    if (!colinear) return;

    const int sf = (df > 0) - (df < 0);   // step direction in file: -1/0/+1
    const int sr = (dr > 0) - (dr < 0);   // step direction in rank

    // between: from a+step up to (not including) b.
    for (int f = fa + sf, r = ra + sr; f != fb || r != rb; f += sf, r += sr)
        set(between, make_square(File(f), Rank(r)));

    // line: walk both directions from a to the board edges (includes a and b).
    for (int f = fa, r = ra; on_board(f, r); f += sf, r += sr) set(line, make_square(File(f), Rank(r)));
    for (int f = fa, r = ra; on_board(f, r); f -= sf, r -= sr) set(line, make_square(File(f), Rank(r)));
}

// -----------------------------------------------------------------------------
// Table storage + one-time fill (provided). The Meyers singleton initializes on
// first access, so callers never need an explicit init step.
// -----------------------------------------------------------------------------
struct Tables {
    Bitboard knight[SQUARE_NB]{};
    Bitboard king[SQUARE_NB]{};
    Bitboard pawn[COLOR_NB][SQUARE_NB]{};
    Bitboard between[SQUARE_NB][SQUARE_NB]{};
    Bitboard line[SQUARE_NB][SQUARE_NB]{};

    Tables() {

        for (Square s = SQ_A1; s <= SQ_H8; s = Square(s + 1)) {
            knight[s]      = compute_knight(s);
            king[s]        = compute_king(s);
            pawn[WHITE][s] = compute_pawn(WHITE, s);
            pawn[BLACK][s] = compute_pawn(BLACK, s);
        }
        for (Square a = SQ_A1; a <= SQ_H8; a = Square(a + 1))
            for (Square b = SQ_A1; b <= SQ_H8; b = Square(b + 1))
                compute_between_line(a, b, between[a][b], line[a][b]);
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

Bitboard between_bb(Square a, Square b) { return tables().between[a][b]; }
Bitboard line_bb(Square a, Square b)    { return tables().line[a][b]; }

} // namespace chess
