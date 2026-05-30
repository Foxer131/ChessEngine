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

    if (g_failures == 0)
        std::cout << "core position checks passed\n";
    else
        std::cout << g_failures << " check(s) FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
