#include "GuiBoard.h"

void GuiBoard::reset() {
    static const char* startRanks[8] = {
        "RNBQKBNR", // rank 1 (white back rank)
        "PPPPPPPP", // rank 2
        "........",
        "........",
        "........",
        "........",
        "pppppppp", // rank 7
        "rnbqkbnr", // rank 8 (black back rank)
    };
    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            board_[rank][file] = startRanks[rank][file];
    whiteToMove_ = true;
}

bool GuiBoard::isOwnPiece(int file, int rank) const {
    char p = board_[rank][file];
    if (p == '.') return false;
    return colorOf(p) == sideToMove();
}

bool GuiBoard::isPromotion(int fromFile, int fromRank, int toFile, int toRank) const {
    (void)toFile;
    char p = board_[fromRank][fromFile];
    if (p == 'P') return toRank == 7;
    if (p == 'p') return toRank == 0;
    return false;
}

QString GuiBoard::toUci(int fromFile, int fromRank, int toFile, int toRank, QChar promo) {
    auto sq = [](int f, int r) {
        return QString(QChar('a' + f)) + QChar('1' + r);
    };
    QString s = sq(fromFile, fromRank) + sq(toFile, toRank);
    if (!promo.isNull()) s += promo;
    return s;
}

bool GuiBoard::applyUci(const QString& uci) {
    if (uci.size() < 4) return false;
    int fromFile = uci[0].toLatin1() - 'a';
    int fromRank = uci[1].toLatin1() - '1';
    int toFile   = uci[2].toLatin1() - 'a';
    int toRank   = uci[3].toLatin1() - '1';
    auto inRange = [](int v) { return v >= 0 && v < 8; };
    if (!inRange(fromFile) || !inRange(fromRank) || !inRange(toFile) || !inRange(toRank))
        return false;

    char piece = board_[fromRank][fromFile];
    if (piece == '.') return false;

    // En passant: a pawn moving diagonally onto an empty square captures the
    // pawn that sits beside the destination (same rank as the origin).
    bool isPawn = (piece == 'P' || piece == 'p');
    if (isPawn && fromFile != toFile && board_[toRank][toFile] == '.')
        board_[fromRank][toFile] = '.';

    // Castling: the king moves two files; relocate the corresponding rook.
    bool isKing = (piece == 'K' || piece == 'k');
    if (isKing && std::abs(toFile - fromFile) == 2) {
        if (toFile == 6) { // king-side: rook h->f
            board_[fromRank][5] = board_[fromRank][7];
            board_[fromRank][7] = '.';
        } else if (toFile == 2) { // queen-side: rook a->d
            board_[fromRank][3] = board_[fromRank][0];
            board_[fromRank][0] = '.';
        }
    }

    // Move the piece.
    board_[fromRank][fromFile] = '.';
    board_[toRank][toFile] = piece;

    // Promotion: 5th char (lowercase q/r/b/n) names the new piece.
    if (uci.size() >= 5) {
        char promo = uci[4].toLower().toLatin1();
        char np = promo;
        if (isWhite(piece)) np = QChar(promo).toUpper().toLatin1();
        board_[toRank][toFile] = np;
    }

    whiteToMove_ = !whiteToMove_;
    return true;
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
