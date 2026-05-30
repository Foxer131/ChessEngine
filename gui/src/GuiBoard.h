#pragma once
// =============================================================================
// GuiBoard - a lightweight board model used ONLY by the GUI for rendering and
// for translating clicks <-> UCI move strings. It is intentionally NOT a rules
// engine: legality (checks, pins, etc.) is the engine's responsibility. The GUI
// applies moves for display and forwards them to the engine over UCI.
//
// Squares are addressed as (file, rank) with file 0..7 = a..h and
// rank 0..7 = 1..8. board[rank][file] holds a FEN-style piece char:
//   white = PNBRQK, black = pnbrqk, empty = '.'.
// =============================================================================

#include <array>
#include <QChar>
#include <QString>

class GuiBoard {
public:
    enum class Color { White, Black };

    GuiBoard() { reset(); }

    void reset();                       // standard start position

    char at(int file, int rank) const { return board_[rank][file]; }
    bool empty(int file, int rank) const { return board_[rank][file] == '.'; }

    static bool isWhite(char p) { return p >= 'A' && p <= 'Z'; }
    static bool isBlack(char p) { return p >= 'a' && p <= 'z'; }
    static Color colorOf(char p) { return isWhite(p) ? Color::White : Color::Black; }

    Color sideToMove() const { return whiteToMove_ ? Color::White : Color::Black; }

    // Does (file,rank) hold a piece of the side to move?
    bool isOwnPiece(int file, int rank) const;

    // True if a pawn on (file,rank) moving to (toFile,toRank) reaches promotion.
    bool isPromotion(int fromFile, int fromRank, int toFile, int toRank) const;

    // Apply a move expressed as a UCI long-algebraic string ("e2e4", "e7e8q",
    // "e1g1" castling, etc.). Updates the board for display and flips the side
    // to move. Returns false if the string is malformed. Does NOT check legality.
    bool applyUci(const QString& uci);

    // Build a UCI move string from coordinates (+ optional promotion piece char,
    // lowercase: q/r/b/n).
    static QString toUci(int fromFile, int fromRank, int toFile, int toRank,
                         QChar promo = QChar());

    // Unicode chess glyph for a FEN piece char, or empty for '.'.
    static QString glyph(char piece);

private:
    std::array<std::array<char, 8>, 8> board_{};
    bool whiteToMove_ = true;
};
