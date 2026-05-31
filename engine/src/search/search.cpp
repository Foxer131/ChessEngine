#include "chess/search.hpp"
#include "chess/movegen.hpp"
#include "chess/movelist.hpp"
#include "chess/eval.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace chess {
namespace {

// Set from another thread (the UCI loop) to abort the running search.
std::atomic<bool> g_stop{false};

constexpr int INF         = 32000;
constexpr int MATE        = 31000;
constexpr int MATE_IN_MAX = MATE - 256;   // scores beyond this are forced mates
constexpr int MAX_PLY     = 128;

// For MVV-LVA ordering and material-aware decisions, indexed by PieceType.
constexpr int PIECE_VAL[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 20000};

// Late-move-reduction amounts, indexed [depth][move number]. Deeper searches and
// later moves get reduced more. Built once at startup.
struct LmrTable {
    int r[64][64];
    LmrTable() {
        for (int d = 0; d < 64; ++d)
            for (int m = 0; m < 64; ++m)
                r[d][m] = (d == 0 || m == 0)
                            ? 0
                            : int(0.5 + std::log(double(d)) * std::log(double(m)) / 2.3);
    }
};
const LmrTable LMR;

// ---- Transposition table ----------------------------------------------------
enum Bound : std::uint8_t { BOUND_NONE, BOUND_EXACT, BOUND_LOWER, BOUND_UPPER };

struct TTEntry {
    std::uint64_t key   = 0;
    Move          move  = MOVE_NONE;
    std::int16_t  score = 0;
    std::int8_t   depth = 0;       // all-zero default keeps g_tt in .bss (no 16MB
    std::uint8_t  bound = BOUND_NONE;  // in the binary); empty slots are found via key mismatch
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

    // Zobrist keys of all positions on the current line (game history + search
    // path). Invariant: back() == pos.key() at every node. Used for repetitions.
    std::vector<std::uint64_t> repList;

    Searcher(Position& p, const SearchLimits& l) : pos(p), limits(l) {}

    // Draw by the 50-move rule or by repetition. In search a single repetition is
    // treated as a draw (if we can repeat once we can repeat again).
    bool is_draw() const {
        if (pos.halfmove_clock() >= 100) return true;
        const int n = static_cast<int>(repList.size());
        const std::uint64_t cur = repList.back();      // == pos.key()
        const int limit = std::min(pos.halfmove_clock(), n - 1);
        for (int d = 2; d <= limit; d += 2)            // same side to move = 2 plies apart
            if (repList[n - 1 - d] == cur) return true;
        return false;
    }

    bool out_of_time() {
        if (stop) return true;
        if (g_stop.load(std::memory_order_relaxed)) { stop = true; return true; }
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

            // Delta pruning: if winning this piece (plus a margin) still can't
            // reach alpha, the capture is hopeless - skip it.
            PieceType victim = (m.type_of() == EN_PASSANT) ? PAWN
                                                           : type_of(pos.piece_on(m.to_sq()));
            int gain = PIECE_VAL[victim] + (m.type_of() == PROMOTION ? 800 : 0);
            if (standPat + gain + 100 < alpha) continue;

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

        if (ply > 0 && is_draw()) return 0;          // repetition / 50-move = draw

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

        const Color us         = pos.side_to_move();
        const bool  pvNode     = (beta - alpha) > 1;
        const int   staticEval = inCheck ? -INF : evaluate(pos);

        // Reverse futility pruning (static null move): if our static eval is so
        // far above beta that even a generous margin can't pull it under, prune.
        if (!pvNode && !inCheck && depth <= 6 && beta < MATE_IN_MAX
            && staticEval - 85 * depth >= beta)
            return staticEval;

        // Null-move pruning: give the opponent a free move; if our position is
        // still good enough to fail high, prune. Skip in check / near-mate /
        // when only pawns remain (zugzwang risk).
        if (!root && !inCheck && depth >= 3 && beta < MATE_IN_MAX
            && has_non_pawn_material(pos, us)) {
            int R = 2 + depth / 6;
            Position::Undo u;
            pos.make_null_move(u);
            repList.push_back(pos.key());
            int score = -negamax(depth - 1 - R, -beta, -beta + 1, ply + 1);
            repList.pop_back();
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
            const bool quiet   = !capture && m.type_of() != PROMOTION;

            Position::Undo u;
            pos.make_move(m, u);
            const bool givesCheck = pos.in_check();

            // Shallow-depth pruning of quiet moves. The bestScore guard keeps the
            // first move (and mate lines) safe: nothing is pruned before we have
            // a real score, and checking moves are never pruned.
            if (!pvNode && !inCheck && !givesCheck && quiet && bestScore > -MATE_IN_MAX) {
                if (depth <= 4 && moveCount > 3 + depth * depth) {           // late move pruning
                    pos.unmake_move(m, u);
                    continue;
                }
                if (depth <= 6 && staticEval + 90 * depth + 50 <= alpha) {   // futility pruning
                    pos.unmake_move(m, u);
                    continue;
                }
            }

            repList.push_back(pos.key());

            int score;
            if (moveCount == 1) {
                score = -negamax(depth - 1, -beta, -alpha, ply + 1);   // PV: full window
            } else {
                int R = 0;  // late move reduction for quiet, non-checking moves
                if (depth >= 3 && moveCount > 3 && quiet && !givesCheck && !inCheck) {
                    R = LMR.r[std::min(depth, 63)][std::min(moveCount, 63)];
                    if (pvNode) --R;
                    R = std::max(0, std::min(R, depth - 2));
                }

                score = -negamax(depth - 1 - R, -alpha - 1, -alpha, ply + 1);  // reduced null window
                if (score > alpha && R > 0)
                    score = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1);  // re-search full depth
                if (score > alpha && score < beta)
                    score = -negamax(depth - 1, -beta, -alpha, ply + 1);       // re-search full window
            }

            repList.pop_back();
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

SearchResult search(Position& pos, const SearchLimits& limits,
                    const std::vector<std::uint64_t>& history) {
    // NOTE: does NOT clear g_stop - the caller must clear_stop() beforehand on
    // the controlling thread (see search.hpp), to avoid racing a `stop`.
    std::memset(g_tt, 0, sizeof(g_tt));
    Searcher s(pos, limits);
    // Seed the repetition list with the game history (its last key is pos.key()).
    if (history.empty()) s.repList = { pos.key() };
    else                 s.repList = history;
    return s.go();
}

void stop_search()  { g_stop.store(true,  std::memory_order_relaxed); }
void clear_stop()   { g_stop.store(false, std::memory_order_relaxed); }

} // namespace chess
