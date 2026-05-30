// Minimal runtime tests for the engine core. No framework: a CHECK macro that
// reports failures and works in any build type (unlike assert(), which the
// Release build's NDEBUG would strip out). Returns non-zero if anything failed,
// so CTest treats a failure as a failing test.

#include <iostream>
#include <sstream>
#include <string>
#include "chess/eval.hpp"
#include "chess/movegen.hpp"
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

// A text fingerprint of a position: board + all state. Used to verify that
// make_move followed by unmake_move restores the position exactly.
static std::string snapshot(const Position& p) {
    std::ostringstream os;
    os << p.to_string()
       << int(p.side_to_move())  << '|' << p.castling_rights() << '|'
       << int(p.ep_square())     << '|' << p.halfmove_clock()  << '|'
       << p.fullmove_number();
    return os.str();
}

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

    // ---- make / unmake ----
    {   // double pawn push sets the en-passant square and flips the side
        Position mp; mp.set_startpos();
        std::string before = snapshot(mp);
        Position::Undo u;
        mp.make_move(Move::make(SQ_E2, SQ_E4), u);
        CHECK(mp.piece_on(SQ_E4) == W_PAWN && mp.empty(SQ_E2));
        CHECK(mp.side_to_move() == BLACK);
        CHECK(mp.ep_square() == SQ_E3);
        mp.unmake_move(Move::make(SQ_E2, SQ_E4), u);
        CHECK(snapshot(mp) == before);
    }
    {   // castling relocates the rook and voids the rights
        Position pp; pp.set_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        Position::Undo u;
        pp.make_move(Move::make(SQ_E1, SQ_G1, CASTLING), u);
        CHECK(pp.piece_on(SQ_G1) == W_KING && pp.piece_on(SQ_F1) == W_ROOK);
        CHECK(pp.empty(SQ_E1) && pp.empty(SQ_H1));
        CHECK((pp.castling_rights() & (WHITE_OO | WHITE_OOO)) == 0);
    }
    {   // en passant removes the bypassed pawn
        Position pp; pp.set_fen("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
        Position::Undo u;
        pp.make_move(Move::make(SQ_E5, SQ_F6, EN_PASSANT), u);
        CHECK(pp.piece_on(SQ_F6) == W_PAWN && pp.empty(SQ_E5) && pp.empty(SQ_F5));
    }
    {   // promotion replaces the pawn, and unmake brings the pawn back
        Position pp; pp.set_fen("7k/P7/8/8/8/8/8/7K w - - 0 1");
        Position::Undo u;
        Move promo = Move::make(SQ_A7, SQ_A8, PROMOTION, QUEEN);
        pp.make_move(promo, u);
        CHECK(pp.piece_on(SQ_A8) == W_QUEEN && pp.empty(SQ_A7));
        pp.unmake_move(promo, u);
        CHECK(pp.piece_on(SQ_A7) == W_PAWN && pp.empty(SQ_A8));
    }
    {   // make then unmake must restore the exact position for every move kind
        struct Case { const char* fen; Move move; };
        const Case cases[] = {
            {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", Move::make(SQ_E4, SQ_D5)},
            {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", Move::make(SQ_E1, SQ_G1, CASTLING)},
            {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", Move::make(SQ_E1, SQ_C1, CASTLING)},
            {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", Move::make(SQ_E5, SQ_F6, EN_PASSANT)},
            {"7k/P7/8/8/8/8/8/7K w - - 0 1", Move::make(SQ_A7, SQ_A8, PROMOTION, QUEEN)},
        };
        for (const Case& c : cases) {
            Position pp; pp.set_fen(c.fen);
            std::string before = snapshot(pp);
            Position::Undo u;
            pp.make_move(c.move, u);
            pp.unmake_move(c.move, u);
            CHECK(snapshot(pp) == before);
        }
    }

    // ---- perft: the move-generation correctness gate ----
    {   // standard start position (reference values from the chess programming wiki)
        Position pf; pf.set_startpos();
        CHECK(perft(pf, 1) == 20);
        CHECK(perft(pf, 2) == 400);
        CHECK(perft(pf, 3) == 8902);
        CHECK(perft(pf, 4) == 197281);
        CHECK(perft(pf, 5) == 4865609);
    }
    {   // "Kiwipete" - dense with castling, en passant, pins; catches subtle bugs
        Position pf;
        pf.set_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        CHECK(perft(pf, 1) == 48);
        CHECK(perft(pf, 2) == 2039);
        CHECK(perft(pf, 3) == 97862);
    }
    {   // position 3 (CPW) - tricky en-passant / promotion interactions
        Position pf;
        pf.set_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        CHECK(perft(pf, 1) == 14);
        CHECK(perft(pf, 2) == 191);
        CHECK(perft(pf, 3) == 2812);
        CHECK(perft(pf, 4) == 43238);
    }

    // ---- evaluation ----
    {   // start position is perfectly symmetric -> exactly 0
        Position e; e.set_startpos();
        CHECK(evaluate(e) == 0);
    }
    {   // black is missing its queen; white to move is up ~900
        Position e; e.set_fen("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        CHECK(evaluate(e) > 800);
    }
    {   // white is missing a rook; black to move is up ~500 (side-to-move view)
        Position e; e.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/1NBQKBNR b KQk - 0 1");
        CHECK(evaluate(e) > 400);
    }

    if (g_failures == 0)
        std::cout << "core position checks passed\n";
    else
        std::cout << g_failures << " check(s) FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
