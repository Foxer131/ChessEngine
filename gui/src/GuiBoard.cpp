#include "GuiBoard.h"

#include <cctype>
#include <string>

#include "chess/movegen.hpp"
#include "chess/movelist.hpp"

namespace {

std::string square_to_uci(chess::Square s) {
    std::string r;
    r += char('a' + chess::file_of(s));
    r += char('1' + chess::rank_of(s));
    return r;
}

// UCI string for a move, matching the engine's format (so we can pair a clicked
// from/to with one of the generated legal moves).
std::string move_to_uci(chess::Move m) {
    std::string s = square_to_uci(m.from_sq()) + square_to_uci(m.to_sq());
    if (m.type_of() == chess::PROMOTION)
        s += "  nbrq"[m.promotion_type()];   // PieceType KNIGHT..QUEEN -> n b r q
    return s;
}

chess::Square sq(int file, int rank) {
    return chess::make_square(chess::File(file), chess::Rank(rank));
}

} // namespace

void GuiBoard::reset() {
    pos_.set_startpos();
}

char GuiBoard::at(int file, int rank) const {
    chess::Piece pc = pos_.piece_on(sq(file, rank));
    if (pc == chess::NO_PIECE) return '.';
    static const char kinds[] = ".PNBRQK";          // index by PieceType
    char c = kinds[chess::type_of(pc)];
    return (chess::color_of(pc) == chess::WHITE) ? c
                                                 : char(std::tolower(c));
}

bool GuiBoard::empty(int file, int rank) const {
    return pos_.piece_on(sq(file, rank)) == chess::NO_PIECE;
}

GuiBoard::Color GuiBoard::sideToMove() const {
    return pos_.side_to_move() == chess::WHITE ? Color::White : Color::Black;
}

bool GuiBoard::isOwnPiece(int file, int rank) const {
    chess::Piece pc = pos_.piece_on(sq(file, rank));
    return pc != chess::NO_PIECE && chess::color_of(pc) == pos_.side_to_move();
}

bool GuiBoard::isPromotion(int fromFile, int fromRank, int toFile, int toRank) const {
    (void)toFile;
    chess::Piece pc = pos_.piece_on(sq(fromFile, fromRank));
    if (chess::type_of(pc) != chess::PAWN) return false;
    return toRank == 7 || toRank == 0;   // reaching either last rank
}

bool GuiBoard::applyUci(const QString& uci) {
    const std::string want = uci.toStdString();
    chess::MoveList legal;
    chess::generate_legal(pos_, legal);
    for (chess::Move m : legal) {
        if (move_to_uci(m) == want) {
            chess::Position::Undo u;       // no takeback yet -> undo is discarded
            pos_.make_move(m, u);
            return true;
        }
    }
    return false;   // illegal or malformed -> reject
}

QString GuiBoard::toUci(int fromFile, int fromRank, int toFile, int toRank, QChar promo) {
    auto s = [](int f, int r) {
        return QString(QChar('a' + f)) + QChar('1' + r);
    };
    QString out = s(fromFile, fromRank) + s(toFile, toRank);
    if (!promo.isNull()) out += promo;
    return out;
}

QString GuiBoard::glyph(char piece) {
    switch (piece) {
        case 'K': return QString(QChar(0x2654));
        case 'Q': return QString(QChar(0x2655));
        case 'R': return QString(QChar(0x2656));
        case 'B': return QString(QChar(0x2657));
        case 'N': return QString(QChar(0x2658));
        case 'P': return QString(QChar(0x2659));
        case 'k': return QString(QChar(0x265A));
        case 'q': return QString(QChar(0x265B));
        case 'r': return QString(QChar(0x265C));
        case 'b': return QString(QChar(0x265D));
        case 'n': return QString(QChar(0x265E));
        case 'p': return QString(QChar(0x265F));
        default:  return QString();
    }
}
