#include "chess/eval.hpp"
#include "chess/bitboard.hpp"

#include <algorithm>

namespace chess {
namespace {

// Material in centipawns, indexed by PieceType (NO_PIECE_TYPE..KING).
constexpr int MATERIAL[PIECE_TYPE_NB] = {
    0,    // NO_PIECE_TYPE
    100,  // PAWN
    320,  // KNIGHT
    330,  // BISHOP
    500,  // ROOK
    900,  // QUEEN
    0     // KING (priceless; not counted as material)
};

// Game-phase weights: opening = 24, deep endgame = 0. Used to taper the king.
constexpr int PHASE_WEIGHT[PIECE_TYPE_NB] = {0, 0, 1, 1, 2, 4, 0};
constexpr int PHASE_MAX = 24;

// How central a square is: 0 on the rim, up to 6 on the four centre squares.
constexpr int centrality(Square s) {
    int f = file_of(s), r = rank_of(s);
    return std::min(f, 7 - f) + std::min(r, 7 - r);
}

// White-perspective positional bonus for a piece on `rel` (already mirrored so
// the formulas can always assume "as if white"), tapered by `phase`.
int piece_square(PieceType pt, Square rel, int phase) {
    const int c = centrality(rel);
    const int r = rank_of(rel);
    switch (pt) {
        case PAWN:   return (r - 1) * 8 + c * 4;     // advance + hold the centre
        case KNIGHT: return c * 8 - 12;              // knights crave the centre
        case BISHOP: return c * 5;
        case ROOK:   return (r == RANK_7) ? 20 : 0;  // 7th-rank rook
        case QUEEN:  return c * 2;
        case KING: {
            const int mg = -c * 12;                  // opening: stay tucked away
            const int eg =  c * 12;                  // endgame: march to the centre
            return (mg * phase + eg * (PHASE_MAX - phase)) / PHASE_MAX;
        }
        default: return 0;
    }
}

} // namespace

int evaluate(const Position& pos) {
    // Game phase from remaining non-pawn material.
    int phase = 0;
    for (PieceType pt = KNIGHT; pt <= QUEEN; pt = PieceType(pt + 1))
        phase += popcount(pos.pieces(pt)) * PHASE_WEIGHT[pt];
    phase = std::min(phase, PHASE_MAX);

    int score = 0;  // white's perspective
    for (Color c = WHITE; c <= BLACK; c = Color(c + 1)) {
        const int sign = (c == WHITE) ? 1 : -1;
        for (PieceType pt = PAWN; pt <= KING; pt = PieceType(pt + 1)) {
            Bitboard b = pos.pieces(c, pt);
            while (b) {
                Square s = pop_lsb(b);
                Square rel = (c == WHITE) ? s : Square(s ^ 56);  // mirror rank for black
                score += sign * (MATERIAL[pt] + piece_square(pt, rel, phase));
            }
        }
    }

    return (pos.side_to_move() == WHITE) ? score : -score;
}

} // namespace chess
