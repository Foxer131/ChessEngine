#include "chess/position.hpp"
#include "chess/attacks.hpp"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>

namespace chess {

namespace {

// --- Zobrist hashing ---------------------------------------------------------
// Random 64-bit keys XOR-folded into a single position hash. Each toggle (a
// piece on a square, the side, a castling-rights state, an ep file) is its own
// key, so flipping any feature is a single XOR - which is what lets us maintain
// the key incrementally in make/unmake.
struct Zobrist {
    std::uint64_t piece[PIECE_NB][SQUARE_NB];
    std::uint64_t castling[16];   // indexed by the 4-bit rights mask
    std::uint64_t epFile[FILE_NB];
    std::uint64_t side;           // XOR'd in when black is to move
};

Zobrist make_zobrist() {
    Zobrist z{};
    std::uint64_t s = 0x9E3779B97F4A7C15ULL;          // fixed seed -> deterministic
    auto next = [&s]() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    };
    for (int p = 0; p < PIECE_NB; ++p)
        for (int sq = 0; sq < SQUARE_NB; ++sq) z.piece[p][sq] = next();
    for (int i = 0; i < 16; ++i)       z.castling[i] = next();
    for (int f = 0; f < FILE_NB; ++f)  z.epFile[f]   = next();
    z.side = next();
    return z;
}

const Zobrist Z = make_zobrist();

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

// Castling rights that SURVIVE when square `s` is vacated or captured on: a king
// leaving e1/e8 voids both of its rights; a rook leaving (or being taken on) a
// corner voids that wing. Other squares change nothing. Used as
//   castlingRights_ &= castle_mask(from) & castle_mask(to).
int castle_mask(Square s) {
    switch (s) {
        case SQ_E1: return ~(WHITE_OO | WHITE_OOO) & ANY_CASTLING;
        case SQ_A1: return ~WHITE_OOO & ANY_CASTLING;
        case SQ_H1: return ~WHITE_OO  & ANY_CASTLING;
        case SQ_E8: return ~(BLACK_OO | BLACK_OOO) & ANY_CASTLING;
        case SQ_A8: return ~BLACK_OOO & ANY_CASTLING;
        case SQ_H8: return ~BLACK_OO  & ANY_CASTLING;
        default:    return ANY_CASTLING;
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
    key_ ^= Z.piece[pc][s];
}

void Position::remove_piece(Square s) {
    Piece pc = board_[s];
    key_ ^= Z.piece[pc][s];
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
// make_move / unmake_move - apply a move and undo it. The Undo record carries
// the state that the move destroys (captured piece, castling rights, ep square,
// clocks) so unmake can restore it exactly.
// -----------------------------------------------------------------------------
void Position::make_move(Move m, Undo& u) {
    const Color    us   = sideToMove_;
    const Color    them = ~us;
    const Square   from = m.from_sq();
    const Square   to   = m.to_sq();
    const MoveType type = m.type_of();
    const Piece    pc   = board_[from];

    // Snapshot the irreversible state for unmake.
    u.captured       = NO_PIECE;
    u.castlingRights = castlingRights_;
    u.epSquare       = epSquare_;
    u.halfmoveClock  = halfmoveClock_;
    u.fullmoveNumber = fullmoveNumber_;
    u.key            = key_;

    bool isCapture = false;

    // Remove the captured piece (en passant captures off the destination square).
    if (type == EN_PASSANT) {
        Square capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
        u.captured = board_[capSq];
        remove_piece(capSq);
        isCapture = true;
    } else if (board_[to] != NO_PIECE) {
        u.captured = board_[to];
        remove_piece(to);
        isCapture = true;
    }

    // Move the piece (promotion places the chosen piece instead of the pawn).
    remove_piece(from);
    if (type == PROMOTION)
        put_piece(make_piece(us, m.promotion_type()), to);
    else
        put_piece(pc, to);

    // Castling also relocates the rook.
    if (type == CASTLING) {
        Rank r = rank_of(from);
        if (file_of(to) == FILE_G) {            // king-side: h-rook -> f
            remove_piece(make_square(FILE_H, r));
            put_piece(make_piece(us, ROOK), make_square(FILE_F, r));
        } else {                                // queen-side: a-rook -> d
            remove_piece(make_square(FILE_A, r));
            put_piece(make_piece(us, ROOK), make_square(FILE_D, r));
        }
    }

    // Update castling rights (king/rook moved, or a rook was captured).
    castlingRights_ &= castle_mask(from) & castle_mask(to);
    key_ ^= Z.castling[u.castlingRights] ^ Z.castling[castlingRights_];

    // En-passant target exists only right after a double pawn push.
    if (u.epSquare != SQ_NONE) key_ ^= Z.epFile[file_of(u.epSquare)];
    epSquare_ = SQ_NONE;
    if (type_of(pc) == PAWN && (to - from == 16 || from - to == 16))
        epSquare_ = Square((from + to) / 2);
    if (epSquare_ != SQ_NONE) key_ ^= Z.epFile[file_of(epSquare_)];

    // 50-move clock: reset on pawn moves and captures, else advance.
    halfmoveClock_ = (type_of(pc) == PAWN || isCapture) ? 0 : halfmoveClock_ + 1;

    if (us == BLACK) ++fullmoveNumber_;
    key_ ^= Z.side;
    sideToMove_ = them;
}

void Position::unmake_move(Move m, const Undo& u) {
    sideToMove_ = ~sideToMove_;          // back to the side that moved
    const Color    us   = sideToMove_;
    const Square   from = m.from_sq();
    const Square   to   = m.to_sq();
    const MoveType type = m.type_of();

    // Move the piece back (promotion turns back into a pawn).
    if (type == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(us, PAWN), from);
    } else {
        Piece pc = board_[to];
        remove_piece(to);
        put_piece(pc, from);
    }

    // Undo the castling rook relocation.
    if (type == CASTLING) {
        Rank r = rank_of(from);
        if (file_of(to) == FILE_G) {
            remove_piece(make_square(FILE_F, r));
            put_piece(make_piece(us, ROOK), make_square(FILE_H, r));
        } else {
            remove_piece(make_square(FILE_D, r));
            put_piece(make_piece(us, ROOK), make_square(FILE_A, r));
        }
    }

    // Put the captured piece back.
    if (type == EN_PASSANT) {
        Square capSq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
        put_piece(make_piece(~us, PAWN), capSq);
    } else if (u.captured != NO_PIECE) {
        put_piece(u.captured, to);
    }

    // Restore the irreversible state.
    castlingRights_ = u.castlingRights;
    epSquare_       = u.epSquare;
    halfmoveClock_  = u.halfmoveClock;
    fullmoveNumber_ = u.fullmoveNumber;
    key_            = u.key;
}

// A null move just passes the turn (used by null-move pruning): no piece moves,
// the en-passant right is dropped, side flips. Never call it while in check.
void Position::make_null_move(Undo& u) {
    u.captured       = NO_PIECE;
    u.castlingRights = castlingRights_;
    u.epSquare       = epSquare_;
    u.halfmoveClock  = halfmoveClock_;
    u.fullmoveNumber = fullmoveNumber_;
    u.key            = key_;

    if (epSquare_ != SQ_NONE) {
        key_ ^= Z.epFile[file_of(epSquare_)];
        epSquare_ = SQ_NONE;
    }
    ++halfmoveClock_;
    if (sideToMove_ == BLACK) ++fullmoveNumber_;
    key_ ^= Z.side;
    sideToMove_ = ~sideToMove_;
}

void Position::unmake_null_move(const Undo& u) {
    sideToMove_     = ~sideToMove_;
    castlingRights_ = u.castlingRights;
    epSquare_       = u.epSquare;
    halfmoveClock_  = u.halfmoveClock;
    fullmoveNumber_ = u.fullmoveNumber;
    key_            = u.key;
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

    // Fold the non-piece state into the key (piece keys were added by put_piece).
    if (sideToMove_ == BLACK) key_ ^= Z.side;
    key_ ^= Z.castling[castlingRights_];
    if (epSquare_ != SQ_NONE) key_ ^= Z.epFile[file_of(epSquare_)];
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

    // Fold the non-piece state into the key (piece keys were added by put_piece).
    if (sideToMove_ == BLACK) key_ ^= Z.side;
    key_ ^= Z.castling[castlingRights_];
    if (epSquare_ != SQ_NONE) key_ ^= Z.epFile[file_of(epSquare_)];
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
