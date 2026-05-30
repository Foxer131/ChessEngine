#include "chess/search.hpp"
#include "chess/movegen.hpp"
#include "chess/movelist.hpp"
#include "chess/eval.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace chess {
namespace {

constexpr int INF         = 32000;
constexpr int MATE        = 31000;
constexpr int MATE_IN_MAX = MATE - 256;   // scores beyond this are forced mates
constexpr int MAX_PLY     = 128;

// For MVV-LVA ordering and material-aware decisions, indexed by PieceType.
constexpr int PIECE_VAL[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 20000};

// ---- Transposition table ----------------------------------------------------
enum Bound : std::uint8_t { BOUND_NONE, BOUND_EXACT, BOUND_LOWER, BOUND_UPPER };

struct TTEntry {
    std::uint64_t key   = 0;
    Move          move  = MOVE_NONE;
    std::int16_t  score = 0;
    std::int8_t   depth = -1;
    std::uint8_t  bound = BOUND_NONE;
};

constexpr std::size_t TT_SIZE = 1 << 20;       // ~16 MB, power of two
TTEntry g_tt[TT_SIZE];

// Mate scores are stored relative to the node (not the root), so the same entry
// is valid at any ply: shift by `ply` on store and unshift on probe.
int to_tt(int score, int ply) {
    if (score >=  MATE_IN_MAX) return score + ply;
    if (score <= -MATE_IN_MAX) return score - ply;
    return score;
}
int from_tt(int score, int ply) {
    if (score >=  MATE_IN_MAX) return score - ply;
    if (score <= -MATE_IN_MAX) return score + ply;
    return score;
}

bool has_non_pawn_material(const Position& p, Color c) {
    return (p.pieces(c, KNIGHT) | p.pieces(c, BISHOP)
          | p.pieces(c, ROOK)   | p.pieces(c, QUEEN)) != 0;
}

// ---- The searcher ------------------------------------------------------------
struct Searcher {
    Position&     pos;
    SearchLimits  limits;
    std::uint64_t nodes = 0;
    bool          stop  = false;
    std::chrono::steady_clock::time_point start;

    Move rootBest = MOVE_NONE;

    Move killers[MAX_PLY][2] = {};
    int  history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    Searcher(Position& p, const SearchLimits& l) : pos(p), limits(l) {}

    bool out_of_time() {
        if (stop) return true;
        if (limits.max_nodes && nodes >= limits.max_nodes) { stop = true; return true; }
        if (limits.movetime_ms > 0 && (nodes & 2047) == 0) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start).count();
            if (ms >= limits.movetime_ms) { stop = true; return true; }
        }
        return false;
    }

    static bool is_capture(const Position& p, Move m) {
        return m.type_of() == EN_PASSANT || p.piece_on(m.to_sq()) != NO_PIECE;
    }

    int score_move(Move m, Move ttMove, int ply, Color us) const {
        if (m == ttMove) return 2'000'000;
        Piece victim = (m.type_of() == EN_PASSANT) ? make_piece(~us, PAWN)
                                                   : pos.piece_on(m.to_sq());
        if (victim != NO_PIECE) {  // capture -> MVV-LVA (value victim, cheap attacker first)
            int attacker = type_of(pos.piece_on(m.from_sq()));
            return 1'000'000 + PIECE_VAL[type_of(victim)] * 16 - attacker;
        }
        if (m.type_of() == PROMOTION) return 900'000 + PIECE_VAL[m.promotion_type()];
        if (m == killers[ply][0])     return 800'000;
        if (m == killers[ply][1])     return 700'000;
        return history[us][m.from_sq()][m.to_sq()];
    }

    // Sort the list in place, best move first (insertion sort; lists are small).
    void order_moves(MoveList& list, Move ttMove, int ply, Color us) {
        const int n = list.size();
        int scores[MoveList::CAPACITY];
        for (int i = 0; i < n; ++i) scores[i] = score_move(list[i], ttMove, ply, us);
        for (int i = 1; i < n; ++i) {
            Move m = list[i];
            int  sc = scores[i], j = i - 1;
            while (j >= 0 && scores[j] < sc) {
                list[j + 1] = list[j]; scores[j + 1] = scores[j]; --j;
            }
            list[j + 1] = m; scores[j + 1] = sc;
        }
    }

    int quiesce(int alpha, int beta, int ply) {
        if (out_of_time()) return 0;
        ++nodes;

        int standPat = evaluate(pos);
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
        if (ply >= MAX_PLY - 1) return alpha;

        MoveList moves;
        generate_legal(pos, moves);
        order_moves(moves, MOVE_NONE, ply, pos.side_to_move());

        for (Move m : moves) {
            if (!is_capture(pos, m) && m.type_of() != PROMOTION) continue;  // captures/promos only
            Position::Undo u;
            pos.make_move(m, u);
            int score = -quiesce(-beta, -alpha, ply + 1);
            pos.unmake_move(m, u);
            if (stop) return 0;
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }

    int negamax(int depth, int alpha, int beta, int ply) {
        if (out_of_time()) return 0;
        ++nodes;

        const bool root    = (ply == 0);
        const bool inCheck = pos.in_check();
        if (inCheck) ++depth;                       // check extension

        if (depth <= 0) return quiesce(alpha, beta, ply);

        // Transposition table probe.
        TTEntry& tte = g_tt[pos.key() & (TT_SIZE - 1)];
        Move ttMove = MOVE_NONE;
        if (tte.key == pos.key()) {
            ttMove = tte.move;
            if (!root && tte.depth >= depth) {
                int s = from_tt(tte.score, ply);
                if (tte.bound == BOUND_EXACT) return s;
                if (tte.bound == BOUND_LOWER && s >= beta)  return s;
                if (tte.bound == BOUND_UPPER && s <= alpha) return s;
            }
        }

        const Color us = pos.side_to_move();

        // Null-move pruning: give the opponent a free move; if our position is
        // still good enough to fail high, prune. Skip in check / near-mate /
        // when only pawns remain (zugzwang risk).
        if (!root && !inCheck && depth >= 3 && beta < MATE_IN_MAX
            && has_non_pawn_material(pos, us)) {
            int R = 2 + depth / 6;
            Position::Undo u;
            pos.make_null_move(u);
            int score = -negamax(depth - 1 - R, -beta, -beta + 1, ply + 1);
            pos.unmake_null_move(u);
            if (stop) return 0;
            if (score >= beta) return beta;
        }

        MoveList moves;
        generate_legal(pos, moves);
        if (moves.size() == 0)                       // checkmate or stalemate
            return inCheck ? -MATE + ply : 0;

        order_moves(moves, ttMove, ply, us);

        int  bestScore = -INF;
        Move bestMove  = MOVE_NONE;
        int  origAlpha = alpha;
        int  moveCount = 0;

        for (Move m : moves) {
            ++moveCount;
            const bool capture = is_capture(pos, m);

            Position::Undo u;
            pos.make_move(m, u);
            const bool givesCheck = pos.in_check();

            int score;
            if (moveCount == 1) {
                score = -negamax(depth - 1, -beta, -alpha, ply + 1);   // PV: full window
            } else {
                int R = 0;  // late move reduction for quiet, non-checking moves
                if (depth >= 3 && moveCount > 3 && !capture && !givesCheck && !inCheck)
                    R = (moveCount > 6) ? 2 : 1;

                score = -negamax(depth - 1 - R, -alpha - 1, -alpha, ply + 1);  // reduced null window
                if (score > alpha && R > 0)
                    score = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1);  // re-search full depth
                if (score > alpha && score < beta)
                    score = -negamax(depth - 1, -beta, -alpha, ply + 1);       // re-search full window
            }

            pos.unmake_move(m, u);
            if (stop) return 0;

            if (score > bestScore) {
                bestScore = score;
                bestMove  = m;
                if (root) rootBest = m;
            }
            if (score > alpha) alpha = score;
            if (alpha >= beta) {                     // beta cutoff
                if (!capture) {                      // remember good quiet moves
                    if (!(killers[ply][0] == m)) {
                        killers[ply][1] = killers[ply][0];
                        killers[ply][0] = m;
                    }
                    history[us][m.from_sq()][m.to_sq()] += depth * depth;
                }
                break;
            }
        }

        // Store in the transposition table.
        Bound flag = (bestScore <= origAlpha) ? BOUND_UPPER
                   : (bestScore >= beta)       ? BOUND_LOWER
                                               : BOUND_EXACT;
        tte = TTEntry{ pos.key(), bestMove,
                       static_cast<std::int16_t>(to_tt(bestScore, ply)),
                       static_cast<std::int8_t>(depth), static_cast<std::uint8_t>(flag) };
        return bestScore;
    }

    SearchResult go() {
        start = std::chrono::steady_clock::now();
        SearchResult result;
        int prevScore = 0;

        for (int d = 1; d <= limits.depth && d < MAX_PLY; ++d) {
            rootBest = MOVE_NONE;

            int alpha = -INF, beta = INF, window = 25;
            if (d >= 4) { alpha = prevScore - window; beta = prevScore + window; }

            int score;
            while (true) {                            // aspiration window with widening
                score = negamax(d, alpha, beta, 0);
                if (stop) break;
                if (score <= alpha)      { window *= 2; alpha = std::max(-INF, score - window); }
                else if (score >= beta)  { window *= 2; beta  = std::min( INF, score + window); }
                else break;
            }

            if (stop) break;                          // discard an incomplete depth

            result.best  = rootBest;
            result.score = score;
            result.depth = d;
            result.nodes = nodes;
            prevScore    = score;

            if (score >= MATE_IN_MAX || score <= -MATE_IN_MAX) break;  // mate found
            if (out_of_time()) break;
        }
        return result;
    }
};

} // namespace

SearchResult search(Position& pos, const SearchLimits& limits) {
    std::memset(g_tt, 0, sizeof(g_tt));
    Searcher s(pos, limits);
    return s.go();
}

} // namespace chess
