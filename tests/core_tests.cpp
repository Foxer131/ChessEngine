// Minimal runtime tests for the engine core. No framework: a CHECK macro that
// reports failures and works in any build type (unlike assert(), which the
// Release build's NDEBUG would strip out). Returns non-zero if anything failed,
// so CTest treats a failure as a failing test.

#include <iostream>
#include "chess/position.hpp"
#include "chess/bitboard.hpp"
#include "chess/move.hpp"
#include "chess/movelist.hpp"
#include "chess/attacks.hpp"

using namespace chess;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL (line " << __LINE__ << "): " #cond "\n";        \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// =============================================================================
// Compile-time checks (relocated here from the headers, which now hold only
// definitions). These fire at build time if a constexpr helper is wrong.
// =============================================================================
namespace compile_time {

// types.hpp
static_assert(make_square(FILE_A, RANK_1) == SQ_A1);
static_assert(make_square(FILE_H, RANK_8) == SQ_H8);
static_assert(make_square(FILE_E, RANK_4) == SQ_E4);
static_assert(file_of(SQ_E4) == FILE_E && rank_of(SQ_E4) == RANK_4);
static_assert(make_piece(WHITE, KNIGHT) == W_KNIGHT);
static_assert(make_piece(BLACK, KING) == B_KING);
static_assert(type_of(B_QUEEN) == QUEEN && color_of(B_QUEEN) == BLACK);
static_assert(~WHITE == BLACK && ~BLACK == WHITE);

// bitboard.hpp
static_assert(square_bb(SQ_A1) == 1ULL && square_bb(SQ_H8) == (1ULL << 63));
static_assert(test(square_bb(SQ_E4), SQ_E4) && !test(square_bb(SQ_E4), SQ_D4));
constexpr bool set_clear_ok() {
    Bitboard b = 0; set(b, SQ_E4); bool was = test(b, SQ_E4); clear(b, SQ_E4);
    return was && b == 0;
}
static_assert(set_clear_ok());
static_assert(popcount(RANK_1_BB) == 8 && popcount(~Bitboard(0)) == 64);
static_assert(lsb(square_bb(SQ_C2)) == SQ_C2);
constexpr int count_by_pop(Bitboard b) { int n = 0; while (b) { (void)pop_lsb(b); ++n; } return n; }
static_assert(count_by_pop(0xF0FULL) == 8);
static_assert(north(square_bb(SQ_E4)) == square_bb(SQ_E5));
static_assert(east(square_bb(SQ_E4)) == square_bb(SQ_F4));
static_assert(east(square_bb(SQ_H4)) == 0 && west(square_bb(SQ_A4)) == 0);  // wrap guards
static_assert(north_east(square_bb(SQ_E4)) == square_bb(SQ_F5));
static_assert(south_west(square_bb(SQ_E4)) == square_bb(SQ_D3));

// move.hpp
static_assert(Move::make(SQ_E2, SQ_E4).from_sq() == SQ_E2);
static_assert(Move::make(SQ_E2, SQ_E4).to_sq() == SQ_E4);
static_assert(Move::make(SQ_E2, SQ_E4).type_of() == NORMAL);
static_assert(Move::make(SQ_E7, SQ_E8, PROMOTION, QUEEN).type_of() == PROMOTION);
static_assert(Move::make(SQ_E7, SQ_E8, PROMOTION, QUEEN).promotion_type() == QUEEN);
static_assert(Move::make(SQ_E1, SQ_G1, CASTLING).type_of() == CASTLING);
static_assert(Move::make(SQ_D5, SQ_E6, EN_PASSANT).type_of() == EN_PASSANT);
static_assert(MOVE_NONE.raw() == 0);

} // namespace compile_time

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

    // ---- MoveList ----
    {
        MoveList ml;
        CHECK(ml.empty());
        ml.add(Move::make(SQ_E2, SQ_E4));
        ml.add(Move::make(SQ_G1, SQ_F3));
        CHECK(ml.size() == 2);
        CHECK(ml[0].to_sq() == SQ_E4);
        int n = 0;
        for (Move m : ml) { (void)m; ++n; }
        CHECK(n == 2);
    }

    // ---- attack tables ----
    CHECK(popcount(knight_attacks(SQ_E4)) == 8);
    CHECK(popcount(knight_attacks(SQ_B1)) == 3);
    CHECK(knight_attacks(SQ_B1) == (square_bb(SQ_A3) | square_bb(SQ_C3) | square_bb(SQ_D2)));
    CHECK(popcount(king_attacks(SQ_E4)) == 8);
    CHECK(popcount(king_attacks(SQ_A1)) == 3);
    CHECK(pawn_attacks(WHITE, SQ_E4) == (square_bb(SQ_D5) | square_bb(SQ_F5)));
    CHECK(pawn_attacks(WHITE, SQ_A2) == square_bb(SQ_B3));   // a-pawn: only one diagonal
    CHECK(pawn_attacks(BLACK, SQ_E5) == (square_bb(SQ_D4) | square_bb(SQ_F4)));

    // ---- sliding attacks (magic bitboards) ----
    CHECK(popcount(rook_attacks(SQ_A1, 0))   == 14);  // empty board, corner
    CHECK(popcount(bishop_attacks(SQ_C1, 0))  == 7);
    CHECK(popcount(queen_attacks(SQ_D4, 0))   == 27);
    {   // rook on a1 blocked by a piece on a4 (file) and c1 (rank)
        Bitboard occ = square_bb(SQ_A4) | square_bb(SQ_C1);
        Bitboard exp = square_bb(SQ_A2) | square_bb(SQ_A3) | square_bb(SQ_A4)
                     | square_bb(SQ_B1) | square_bb(SQ_C1);
        CHECK(rook_attacks(SQ_A1, occ) == exp);       // stops on (and includes) the blocker
    }
    {   // bishop on d4 blocked at f6 (NE) and b2 (SW)
        Bitboard occ = square_bb(SQ_F6) | square_bb(SQ_B2);
        Bitboard a = bishop_attacks(SQ_D4, occ);
        CHECK((a & square_bb(SQ_F6)) != 0);           // blocker is attacked
        CHECK((a & square_bb(SQ_G7)) == 0);           // square beyond it is not
        CHECK((a & square_bb(SQ_B2)) != 0);
        CHECK((a & square_bb(SQ_A1)) == 0);
    }

    // ---- attackers_to / checks ----
    CHECK(p.king_square(WHITE) == SQ_E1);
    CHECK(p.king_square(BLACK) == SQ_E8);
    CHECK(!p.in_check());                                  // startpos: nobody in check
    CHECK(p.is_attacked(SQ_F3, WHITE));                    // g1 knight + e2/g2 pawns
    CHECK(!p.is_attacked(SQ_E2, BLACK));
    CHECK(popcount(p.attackers_to(SQ_F3, p.pieces()) & p.pieces(WHITE)) == 3);
    {
        Position c;
        c.set_fen("4k3/8/8/8/8/8/4Q3/4K3 b - - 0 1");     // white queen checks black king
        CHECK(c.in_check());
        CHECK(c.is_attacked(SQ_E8, WHITE));
        CHECK(!c.is_attacked(SQ_A1, BLACK));
    }

    if (g_failures == 0)
        std::cout << "core position checks passed\n";
    else
        std::cout << g_failures << " check(s) FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
