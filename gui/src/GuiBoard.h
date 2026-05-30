#pragma once
// =============================================================================
// GuiBoard - the GUI's board model. It wraps the engine library's Position
// (chess_core), so it is the AUTHORITATIVE rules model: applyUci() only accepts
// LEGAL moves (using the perft-validated generator). The engine process is still
// driven over UCI for its *thinking*; this just keeps the GUI honest.
//
// Squares are addressed as (file, rank) with file 0..7 = a..h, rank 0..7 = 1..8.
// at() returns a FEN-style piece char: white = PNBRQK, black = pnbrqk, '.' empty.
// =============================================================================

#include <QChar>
#include <QString>
#include "chess/position.hpp"

class GuiBoard {
public:
    enum class Color { White, Black };

    GuiBoard() { reset(); }

    void reset();                       // standard start position

    char at(int file, int rank) const;          // FEN char ('.' if empty)
    bool empty(int file, int rank) const;

    static bool isWhite(char p) { return p >= 'A' && p <= 'Z'; }
    static bool isBlack(char p) { return p >= 'a' && p <= 'z'; }
    static Color colorOf(char p) { return isWhite(p) ? Color::White : Color::Black; }

    Color sideToMove() const;

    // Does (file,rank) hold a piece of the side to move?
    bool isOwnPiece(int file, int rank) const;

    // True if a pawn on (file,rank) moving to (toFile,toRank) reaches promotion.
    bool isPromotion(int fromFile, int fromRank, int toFile, int toRank) const;

    // Apply a move (UCI long-algebraic, e.g. "e2e4", "e7e8q", "e1g1") ONLY if it
    // is legal in the current position. Returns false (and changes nothing) for
    // illegal or malformed input.
    bool applyUci(const QString& uci);

    // Build a UCI move string from coordinates (+ optional promotion piece char,
    // lowercase: q/r/b/n).
    static QString toUci(int fromFile, int fromRank, int toFile, int toRank,
                         QChar promo = QChar());

    // Unicode chess glyph for a FEN piece char, or empty for '.'.
    static QString glyph(char piece);

private:
    chess::Position pos_;
};
