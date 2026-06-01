#include "chess/movegen.hpp"
#include "chess/attacks.hpp"
#include "chess/bitboard.hpp"

namespace chess {
namespace {

// Append a NORMAL move from `from` to each set square in `targets`.
void add_targets(MoveList& list, Square from, Bitboard targets) {
    while (targets)
        list.add(Move::make(from, pop_lsb(targets)));
}

// A pawn reaching the last rank: emit all four under-/promotions.
void add_promotions(MoveList& list, Square from, Square to) {
    list.add(Move::make(from, to, PROMOTION, QUEEN));
    list.add(Move::make(from, to, PROMOTION, ROOK));
    list.add(Move::make(from, to, PROMOTION, BISHOP));
    list.add(Move::make(from, to, PROMOTION, KNIGHT));
}

void generate_castling(const Position& pos, MoveList& list, Color us) {
    const Color them = ~us;
    if (us == WHITE) {
        if ((pos.castling_rights() & WHITE_OO)
            && pos.empty(SQ_F1) && pos.empty(SQ_G1)
            && !pos.is_attacked(SQ_E1, them) && !pos.is_attacked(SQ_F1, them)
            && !pos.is_attacked(SQ_G1, them))
            list.add(Move::make(SQ_E1, SQ_G1, CASTLING));
        if ((pos.castling_rights() & WHITE_OOO)
            && pos.empty(SQ_B1) && pos.empty(SQ_C1) && pos.empty(SQ_D1)
            && !pos.is_attacked(SQ_E1, them) && !pos.is_attacked(SQ_D1, them)
            && !pos.is_attacked(SQ_C1, them))
            list.add(Move::make(SQ_E1, SQ_C1, CASTLING));
    } else {
        if ((pos.castling_rights() & BLACK_OO)
            && pos.empty(SQ_F8) && pos.empty(SQ_G8)
            && !pos.is_attacked(SQ_E8, them) && !pos.is_attacked(SQ_F8, them)
            && !pos.is_attacked(SQ_G8, them))
            list.add(Move::make(SQ_E8, SQ_G8, CASTLING));
        if ((pos.castling_rights() & BLACK_OOO)
            && pos.empty(SQ_B8) && pos.empty(SQ_C8) && pos.empty(SQ_D8)
            && !pos.is_attacked(SQ_E8, them) && !pos.is_attacked(SQ_D8, them)
            && !pos.is_attacked(SQ_C8, them))
            list.add(Move::make(SQ_E8, SQ_C8, CASTLING));
    }
}

// Pseudo-legal moves: everything the pieces can do, NOT yet filtered for leaving
// the own king in check. generate_legal does that filtering.
void generate_pseudo(const Position& pos, MoveList& list) {
    const Color    us    = pos.side_to_move();
    const Color    them  = ~us;
    const Bitboard own   = pos.pieces(us);
    const Bitboard enemy = pos.pieces(them);
    const Bitboard occ   = pos.pieces();

    // Knights
    Bitboard bb = pos.pieces(us, KNIGHT);
    while (bb) { Square s = pop_lsb(bb); add_targets(list, s, knight_attacks(s) & ~own); }

    // Bishops + queens (diagonal rays)
    bb = pos.pieces(us, BISHOP) | pos.pieces(us, QUEEN);
    while (bb) { Square s = pop_lsb(bb); add_targets(list, s, bishop_attacks(s, occ) & ~own); }

    // Rooks + queens (straight rays)
    bb = pos.pieces(us, ROOK) | pos.pieces(us, QUEEN);
    while (bb) { Square s = pop_lsb(bb); add_targets(list, s, rook_attacks(s, occ) & ~own); }

    // King (castling handled separately)
    Square ks = pos.king_square(us);
    add_targets(list, ks, king_attacks(ks) & ~own);

    // Pawns
    const int  push      = (us == WHITE) ? 8 : -8;
    const Rank startRank = (us == WHITE) ? RANK_2 : RANK_7;
    const Rank promoRank = (us == WHITE) ? RANK_8 : RANK_1;

    bb = pos.pieces(us, PAWN);
    while (bb) {
        Square s = pop_lsb(bb);

        // Single (and double) push onto empty squares.
        Square one = Square(s + push);
        if (pos.empty(one)) {
            if (rank_of(one) == promoRank) {
                add_promotions(list, s, one);
            } else {
                list.add(Move::make(s, one));
                Square two = Square(s + 2 * push);
                if (rank_of(s) == startRank && pos.empty(two))
                    list.add(Move::make(s, two));
            }
        }

        // Diagonal captures (pawn_attacks already handles the edge files).
        Bitboard caps = pawn_attacks(us, s) & enemy;
        while (caps) {
            Square t = pop_lsb(caps);
            if (rank_of(t) == promoRank) add_promotions(list, s, t);
            else                          list.add(Move::make(s, t));
        }

        // En passant.
        if (pos.ep_square() != SQ_NONE
            && (pawn_attacks(us, s) & square_bb(pos.ep_square())))
            list.add(Move::make(s, pos.ep_square(), EN_PASSANT));
    }

    generate_castling(pos, list, us);
}

} // namespace

void generate_legal(Position& pos, MoveList& list) {
    MoveList pseudo;
    generate_pseudo(pos, pseudo);

    const Color    us   = pos.side_to_move();
    const Color    them = ~us;
    const Square   ksq  = pos.king_square(us);
    const Bitboard occ  = pos.pieces();

    // Enemy pieces giving check, and how many.
    const Bitboard checkers    = pos.attackers_to(ksq, occ) & pos.pieces(them);
    const int      numCheckers = popcount(checkers);

    // Pinned own pieces: for each enemy slider aligned with our king, if exactly one
    // piece sits between them and it's ours, that piece is pinned to the king ray.
    Bitboard pinned  = 0;
    Bitboard pinners = ((pos.pieces(them, BISHOP) | pos.pieces(them, QUEEN)) & bishop_attacks(ksq, 0))
                     | ((pos.pieces(them, ROOK)   | pos.pieces(them, QUEEN)) & rook_attacks(ksq, 0));
    while (pinners) {
        Square sq = pop_lsb(pinners);
        Bitboard b = between_bb(ksq, sq) & occ;
        if (popcount(b) == 1 && (b & pos.pieces(us)))
            pinned |= b;
    }

    // In single check, a non-king move is legal only if it lands on the checker or
    // interposes between it and the king. In double check, only the king can move.
    Bitboard checkMask = ~Bitboard(0);
    if (numCheckers == 1)
        checkMask = checkers | between_bb(ksq, lsb(checkers));

    for (Move m : pseudo) {
        const Square   from = m.from_sq();
        const Square   to   = m.to_sq();
        const MoveType ty   = m.type_of();

        if (from == ksq) {
            // King move: destination must be unattacked with the king removed from
            // occupancy (so an enemy slider sees through the king's old square).
            // Castling is rejected while in check (its path squares were already
            // verified safe in generate_castling).
            if (numCheckers && ty == CASTLING) continue;
            Bitboard occNoKing = occ ^ square_bb(ksq);
            if (!(pos.attackers_to(to, occNoKing) & pos.pieces(them)))
                list.add(m);
            continue;
        }

        if (numCheckers >= 2) continue;   // double check: only king moves

        // En passant can expose the king along a rank (both pawns leave it at once),
        // which pins don't catch. Verify this rare case with make/unmake.
        if (ty == EN_PASSANT) {
            Position::Undo u;
            pos.make_move(m, u);
            bool ok = !pos.is_attacked(pos.king_square(us), them);
            pos.unmake_move(m, u);
            if (ok) list.add(m);
            continue;
        }

        // Must resolve the check (capture/block) if in check.
        if (!(square_bb(to) & checkMask)) continue;

        // A pinned piece may only move along the king<->piece line.
        if ((square_bb(from) & pinned) && !(square_bb(to) & line_bb(ksq, from)))
            continue;

        list.add(m);
    }
}

std::uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;

    MoveList moves;
    generate_legal(pos, moves);
    if (depth == 1) return static_cast<std::uint64_t>(moves.size());

    std::uint64_t nodes = 0;
    for (Move m : moves) {
        Position::Undo u;
        pos.make_move(m, u);
        nodes += perft(pos, depth - 1);
        pos.unmake_move(m, u);
    }
    return nodes;
}

} // namespace chess
