// Minimal runtime tests for the engine core. No framework: a CHECK macro that
// reports failures and works in any build type (unlike assert(), which the
// Release build's NDEBUG would strip out). Returns non-zero if anything failed,
// so CTest treats a failure as a failing test.

#include <iostream>
#include "chess/position.hpp"
#include "chess/bitboard.hpp"

using namespace chess;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL (line " << __LINE__ << "): " #cond "\n";        \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int main() {
    Position p;
    p.set_startpos();

    // You should see the standard starting position printed here:
    std::cout << p.to_string() << '\n';

    CHECK(p.side_to_move() == WHITE);

    // Mailbox spot-checks
    CHECK(p.piece_on(SQ_E1) == W_KING);
    CHECK(p.piece_on(SQ_D1) == W_QUEEN);
    CHECK(p.piece_on(SQ_A1) == W_ROOK);
    CHECK(p.piece_on(SQ_D8) == B_QUEEN);
    CHECK(p.piece_on(SQ_E4) == NO_PIECE);
    CHECK(p.empty(SQ_E4));
    CHECK(!p.empty(SQ_E1));

    // Bitboard counts
    CHECK(popcount(p.pieces())            == 32);
    CHECK(popcount(p.pieces(WHITE))       == 16);
    CHECK(popcount(p.pieces(BLACK))       == 16);
    CHECK(popcount(p.pieces(PAWN))        == 16);
    CHECK(popcount(p.pieces(WHITE, PAWN)) == 8);
    CHECK(popcount(p.pieces(WHITE, KING)) == 1);

    // ---- FEN parsing ----
    {   // start position via FEN should match set_startpos
        Position f;
        f.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        CHECK(popcount(f.pieces()) == 32);
        CHECK(f.piece_on(SQ_E1) == W_KING);
        CHECK(f.piece_on(SQ_D8) == B_QUEEN);
        CHECK(f.side_to_move() == WHITE);
        CHECK(f.castling_rights() == ANY_CASTLING);
        CHECK(f.ep_square() == SQ_NONE);
        CHECK(f.halfmove_clock() == 0);
        CHECK(f.fullmove_number() == 1);
    }
    {   // after 1.e4: black to move, pawn on e4, e2 empty, en-passant target e3
        Position f;
        f.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
        CHECK(f.side_to_move() == BLACK);
        CHECK(f.piece_on(SQ_E4) == W_PAWN);
        CHECK(f.empty(SQ_E2));
        CHECK(f.ep_square() == SQ_E3);
    }
    {   // sparse position: only white king-side castling, clocks 5 and 10
        Position f;
        f.set_fen("4k3/8/8/8/8/8/8/4K2R w K - 5 10");
        CHECK(popcount(f.pieces()) == 3);
        CHECK(f.piece_on(SQ_H1) == W_ROOK);
        CHECK(f.piece_on(SQ_E8) == B_KING);
        CHECK(f.castling_rights() == WHITE_OO);
        CHECK(f.halfmove_clock() == 5);
        CHECK(f.fullmove_number() == 10);
    }

    if (g_failures == 0)
        std::cout << "core position checks passed\n";
    else
        std::cout << g_failures << " check(s) FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
