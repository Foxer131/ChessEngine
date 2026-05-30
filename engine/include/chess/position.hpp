#pragma once
// =============================================================================
// chess/position.hpp - the board state.
//
// We keep the board in BOTH forms at once, kept in sync by put_piece/remove_piece:
//   * bitboards  - byColor_[c] and byType_[pt]: fast "where are all the X?" sets.
//                  Any specific set is an intersection, e.g. white knights =
//                  byColor_[WHITE] & byType_[KNIGHT].
//   * mailbox    - board_[64]: fast "what is on square s?" (a single Piece).
// Redundant on purpose: each answers a different question in O(1).
//
// This step covers piece placement + queries. Castling rights, en-passant
// square, move clocks and FEN parsing arrive in the next step (3b).
//
// Fill in the `// TODO`s: the inline queries below, and put_piece/remove_piece
// in position.cpp.
// =============================================================================

#include <string>
#include "chess/types.hpp"
#include "chess/bitboard.hpp"
#include "chess/move.hpp"

namespace chess {

class Position {
public:
    // ---- setup (provided in position.cpp) ----
    void reset();         // empty the board (named reset, not clear - see note below)
    void set_startpos();  // standard chess starting position
    void set_fen(const std::string& fen);  // load a position from a FEN string

    // The piece sitting on square s (NO_PIECE if empty).
    Piece piece_on(Square s) const {
        return board_[s];
    }

    // Is square s empty?
    bool empty(Square s) const { return board_[s] == NO_PIECE; }

    // Whose turn is it.
    Color side_to_move() const { return sideToMove_; }

    // All occupied squares (both colors).
    Bitboard pieces() const { return byColor_[WHITE] | byColor_[BLACK]; }

    // All squares occupied by color c.
    Bitboard pieces(Color c) const { return byColor_[c]; }

    // All squares occupied by piece type pt (either color).
    Bitboard pieces(PieceType pt) const { return byType_[pt]; }

    // Squares occupied by color c AND type pt (e.g. white knights).
    Bitboard pieces(Color c, PieceType pt) const { return byColor_[c] & byType_[pt]; }

    // ---- extra state (provided getters) ----
    int    castling_rights() const { return castlingRights_; } // bitmask of CastlingRights
    Square ep_square() const       { return epSquare_; }       // SQ_NONE if no en-passant
    int    halfmove_clock() const  { return halfmoveClock_; }  // for the 50-move rule
    int    fullmove_number() const { return fullmoveNumber_; } // starts at 1

    // ---- attack / check queries ----
    // Every piece of EITHER color that attacks square `s`, given `occupied`.
    // (fill in - in position.cpp)
    Bitboard attackers_to(Square s, Bitboard occupied) const;

    // The square of color c's king.
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }

    // Is square `s` attacked by any piece of color `by`?
    bool is_attacked(Square s, Color by) const {
        return (attackers_to(s, pieces()) & pieces(by)) != 0;
    }

    // Is the side-to-move's king currently in check?
    bool in_check() const {
        return is_attacked(king_square(side_to_move()), ~side_to_move());
    }

    // ---- make / unmake (apply and undo a move) ----
    // State that a move destroys and unmake must restore.
    struct Undo {
        Piece  captured       = NO_PIECE;  // piece taken (incl. en-passant), or NO_PIECE
        int    castlingRights = NO_CASTLING;
        Square epSquare       = SQ_NONE;
        int    halfmoveClock  = 0;
        int    fullmoveNumber = 1;
    };
    void make_move(Move m, Undo& u);          // apply m, recording undo info in u
    void unmake_move(Move m, const Undo& u);  // restore the position before m

    // ---- board edits (fill in - in position.cpp) ----
    void put_piece(Piece pc, Square s);   // place pc on s (s must be empty)
    void remove_piece(Square s);          // remove whatever is on s

    // ---- debug (provided in position.cpp) ----
    std::string to_string() const;        // ASCII board, rank 8 on top

private:
    Bitboard byColor_[COLOR_NB]      = {}; // index by Color
    Bitboard byType_[PIECE_TYPE_NB]  = {}; // index by PieceType (PAWN..KING; 0 unused)
    Piece    board_[SQUARE_NB]       = {}; // mailbox; NO_PIECE (=0) everywhere by default
    Color    sideToMove_             = WHITE;
    int      castlingRights_         = NO_CASTLING; // bitmask of CastlingRights
    Square   epSquare_               = SQ_NONE;     // en-passant target, or SQ_NONE
    int      halfmoveClock_          = 0;
    int      fullmoveNumber_         = 1;
};

}
