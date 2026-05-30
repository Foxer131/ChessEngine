#include "chess/position.hpp"

#include <sstream>

namespace chess {

void Position::reset() {
    *this = Position{};   // reset every member to its default
}

// -----------------------------------------------------------------------------
// put_piece / remove_piece - the ONLY places that touch the raw arrays. Both
// representations (bitboards + mailbox) must stay in sync, so each edit updates
// three things. Everything else in the engine goes through these two.
// -----------------------------------------------------------------------------

void Position::put_piece(Piece pc, Square s) {
    board_[s] = pc;
    set(byColor_[color_of(pc)], s);
    set(byType_[type_of(pc)], s);
}

void Position::remove_piece(Square s) {
    Piece pc = board_[s];
    clear(byColor_[color_of(pc)], s);
    clear(byType_[type_of(pc)], s);
    board_[s] = NO_PIECE;
}

// -----------------------------------------------------------------------------
// Provided helpers
// -----------------------------------------------------------------------------

void Position::set_startpos() {
    reset();
    // Back-rank piece order, file a -> h.
    static const PieceType back[FILE_NB] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };

    for (File f = FILE_A; f <= FILE_H; f = File(f + 1)) {
        put_piece(make_piece(WHITE, back[f]), make_square(f, RANK_1));
        put_piece(make_piece(WHITE, PAWN),    make_square(f, RANK_2));
        put_piece(make_piece(BLACK, PAWN),    make_square(f, RANK_7));
        put_piece(make_piece(BLACK, back[f]), make_square(f, RANK_8));
    }

    sideToMove_ = WHITE;
}

std::string Position::to_string() const {
    // Glyphs indexed directly by the Piece value (NO_PIECE=0 .. B_KING=14).
    static const char glyphs[] = " PNBRQK  pnbrqk";
    std::ostringstream os;
    
    for (Rank r = RANK_8; r >= RANK_1; r = Rank(r - 1)) {
        os << (r + 1) << "  ";

        for (File f = FILE_A; f <= FILE_H; f = File(f + 1))
            os << glyphs[piece_on(make_square(f, r))] << ' ';
        
        os << '\n';
    }
    
    os << "\n   a b c d e f g h\n";
    
    return os.str();
}

} // namespace chess
