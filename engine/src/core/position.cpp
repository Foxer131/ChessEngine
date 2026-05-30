#include "chess/position.hpp"
#include "chess/attacks.hpp"

#include <cctype>
#include <sstream>
#include <string>

namespace chess {

namespace {

// Map a FEN piece letter to a Piece. The case carries the color (uppercase =
// white, lowercase = black); the letter carries the type. 'N' -> W_KNIGHT,
// 'q' -> B_QUEEN, etc. Returns NO_PIECE for an unrecognized character.
Piece piece_from_char(char c) {
    Color col = std::isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;
    
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'p': 
            return make_piece(col, PAWN);
        case 'n': 
            return make_piece(col, KNIGHT);
        case 'b': 
            return make_piece(col, BISHOP);
        case 'r': 
            return make_piece(col, ROOK);
        case 'q': 
            return make_piece(col, QUEEN);
        case 'k': 
            return make_piece(col, KING);
        default:  
            return NO_PIECE;
    }
}

} // namespace

void Position::reset() {
    *this = Position{};   // reset every member to its default
}

// -----------------------------------------------------------------------------
// put_piece / remove_piece - the ONLY places that touch the raw arrays. Both
// representations (bitboards + mailbox) must stay in sync, so each edit updates
// all three. Everything else in the engine goes through these two.
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
// attackers_to - the set of ALL pieces (both colors) that attack square `s`.
// -----------------------------------------------------------------------------
Bitboard Position::attackers_to(Square s, Bitboard occupied) const {
    // PSEUDOCODE - for each piece kind, intersect "squares that kind attacks
    // FROM s" with "where those pieces actually sit", then OR the parts.
    //
    //   knights        : knight_attacks(s)            AND pieces(KNIGHT)
    //   kings          : king_attacks(s)              AND pieces(KING)
    //   diagonal sliders: bishop_attacks(s, occupied) AND (pieces(BISHOP) | pieces(QUEEN))
    //   straight sliders: rook_attacks(s, occupied)   AND (pieces(ROOK)   | pieces(QUEEN))
    //
    //   pawns (the subtle bit - pawn attacks are DIRECTIONAL, so swap the color):
    //     a white pawn hits `s` from the squares a BLACK pawn on `s` would
    //     attack, and vice-versa. So:
    //       white pawn attackers : pawn_attacks(BLACK, s) AND pieces(WHITE, PAWN)
    //       black pawn attackers : pawn_attacks(WHITE, s) AND pieces(BLACK, PAWN)
    //
    // OR everything above together and return it.

    Bitboard b1 = knight_attacks(s) & pieces(KNIGHT);
    Bitboard b2 = king_attacks(s) & pieces(KING);
    Bitboard b3 = bishop_attacks(s, occupied) & (pieces(BISHOP) | pieces(QUEEN));
    Bitboard b4 = rook_attacks(s, occupied) & (pieces(ROOK) | pieces(QUEEN));

    Bitboard b5 = pawn_attacks(BLACK, s) & pieces(WHITE, PAWN);
    Bitboard b6 = pawn_attacks(WHITE, s) & pieces(BLACK, PAWN);

    return b1 | b2 | b3 | b4 | b5 | b6;
}

// -----------------------------------------------------------------------------
// Position setup
// -----------------------------------------------------------------------------

void Position::set_startpos() {
    reset();
    static const PieceType back[FILE_NB] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };
    for (File f = FILE_A; f <= FILE_H; f = File(f + 1)) {
        put_piece(make_piece(WHITE, back[f]), make_square(f, RANK_1));
        put_piece(make_piece(WHITE, PAWN),    make_square(f, RANK_2));
        put_piece(make_piece(BLACK, PAWN),    make_square(f, RANK_7));
        put_piece(make_piece(BLACK, back[f]), make_square(f, RANK_8));
    }
    sideToMove_     = WHITE;
    castlingRights_ = ANY_CASTLING;
    epSquare_       = SQ_NONE;
    halfmoveClock_  = 0;
    fullmoveNumber_ = 1;
}

// Load a position from a FEN string. A FEN has 6 space-separated fields:
//   1) piece placement  2) side to move  3) castling  4) en-passant
//   5) halfmove clock    6) fullmove number
//   e.g. "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
// Assumes well-formed input.
void Position::set_fen(const std::string& fen) {
    reset();

    std::istringstream ss(fen);
    std::string placement, stm, castling, ep;
    int half = 0, full = 1;
    ss >> placement >> stm >> castling >> ep >> half >> full;

    // Field 1: piece placement. Listed rank 8 -> 1; within a rank, file a -> h.
    File file = FILE_A;
    Rank rank = RANK_8;
    for (char ch : placement) {
        if (ch == '/') {                        // end of a rank
            rank = Rank(rank - 1);
            file = FILE_A;
        } else if (ch >= '1' && ch <= '8') {    // run of empty squares
            file = File(file + (ch - '0'));
        } else {                                // a piece
            put_piece(piece_from_char(ch), make_square(file, rank));
            file = File(file + 1);
        }
    }

    // Field 2: side to move.
    sideToMove_ = (stm == "w") ? WHITE : BLACK;

    // Field 3: castling rights (any subset of KQkq, or "-").
    if (castling != "-") {
        for (char ch : castling) {
            switch (ch) {
                case 'K': castlingRights_ |= WHITE_OO;  break;
                case 'Q': castlingRights_ |= WHITE_OOO; break;
                case 'k': castlingRights_ |= BLACK_OO;  break;
                case 'q': castlingRights_ |= BLACK_OOO; break;
            }
        }
    }

    // Field 4: en-passant target square (algebraic like "e3", or "-").
    if (ep != "-")
        epSquare_ = make_square(File(ep[0] - 'a'), Rank(ep[1] - '1'));

    // Fields 5 & 6: move clocks.
    halfmoveClock_  = half;
    fullmoveNumber_ = full;
}

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

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
