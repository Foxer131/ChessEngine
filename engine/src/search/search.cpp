#include "chess/search.hpp"
#include "chess/movegen.hpp"
#include "chess/movelist.hpp"
#include "chess/eval.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

// =============================================================================
// Architecture (designed so multithreading does NOT make later changes harder):
//
//   TranspositionTable - the ONE component shared between threads. Lockless:
//                        entries are validated by key, torn writes are tolerated.
//   SharedState        - everything shared, injected by reference into each
//                        Worker (the TT, the limits, the stop flag, the game
//                        history). Constructor injection = dependency injection.
//   Worker             - all PER-THREAD mutable state (its own Position copy,
//                        history/killers/counters, repetition list, node count)
//                        plus the search itself as methods. N threads = N Workers.
//
// Because every search heuristic lives on Worker (per-thread) and the only shared
// object is the TT (accessed through a narrow lockless interface), adding a new
// heuristic never requires reasoning about concurrency: just add a field to
// Worker. Threads=1 instantiates exactly one Worker and is identical to the old
// single-threaded search.
// =============================================================================

namespace chess {
namespace {

// The external abort flag (UCI `stop` / start-over). Injected into SharedState.
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
    std::int8_t   depth = 0;
    std::uint8_t  bound = BOUND_NONE;
};

// The shared, lockless transposition table. One bucket per key (always-replace).
// Reads are validated by the full key, so a torn concurrent write just looks
// like a miss or is caught by the key check - acceptable for Lazy SMP.
class TranspositionTable {
public:
    TranspositionTable() { resize(16); }   // 16 MB default (UCI Hash option)

    // (Re)allocate to the largest power-of-two entry count fitting in `mb`
    // megabytes, and clear. A bigger table = fewer collisions, which matters more
    // the deeper / more-threaded the search (many threads hammering one TT).
    void resize(std::size_t mb) {
        std::size_t n = (mb * 1024 * 1024) / sizeof(TTEntry);
        std::size_t p = 1;
        while ((p << 1) <= n) p <<= 1;     // round down to a power of two
        table_.assign(p, TTEntry{});
        mask_ = table_.size() - 1;
    }

    void clear() { std::fill(table_.begin(), table_.end(), TTEntry{}); }

    TTEntry* probe(std::uint64_t key, bool& hit) {
        TTEntry* e = &table_[key & mask_];
        hit = (e->key == key);
        return e;
    }

    // Pull the bucket into cache ahead of the probe (hardware prefetch).
    void prefetch(std::uint64_t key) const {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&table_[key & mask_]);
#endif
    }

private:
    std::vector<TTEntry> table_;
    std::size_t          mask_ = 0;
};

TranspositionTable g_tt;   // the single shared table (kept across moves)

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

// Static Exchange Evaluation: the material swing if the capture `m` is followed
// by the best sequence of recaptures on its target square (swap algorithm). A
// negative result means the capture loses material. attackers_to is recomputed
// with an updated occupancy each step, which naturally reveals X-ray attackers.
// Pure function of the position - no per-thread state, safe to share.
int see(const Position& pos, Move m) {
    const Square from = m.from_sq();
    const Square to   = m.to_sq();
    const bool   ep   = (m.type_of() == EN_PASSANT);

    PieceType victim = ep ? PAWN : type_of(pos.piece_on(to));
    if (victim == NO_PIECE_TYPE) return 0;   // not a capture

    int gain[32];
    int d = 0;
    gain[0] = PIECE_VAL[victim];

    PieceType aPiece  = type_of(pos.piece_on(from));   // piece left standing on `to`
    Bitboard  fromSet = square_bb(from);
    Bitboard  occ     = pos.pieces();
    if (ep) occ ^= square_bb(Square(pos.side_to_move() == WHITE ? to - 8 : to + 8));

    Color side = ~pos.side_to_move();        // side to recapture

    while (true) {
        ++d;
        gain[d] = PIECE_VAL[aPiece] - gain[d - 1];
        if (std::max(-gain[d - 1], gain[d]) < 0) break;   // even optimistically losing

        occ ^= fromSet;                                    // the capturer leaves its square
        Bitboard attackers = pos.attackers_to(to, occ) & occ;
        Bitboard mine = attackers & pos.pieces(side);
        if (!mine) break;

        PieceType pt = PAWN;                               // least valuable attacker
        Bitboard sel = 0;
        for (; pt <= KING; pt = PieceType(pt + 1)) {
            sel = mine & pos.pieces(pt);
            if (sel) break;
        }
        if (pt == KING && (attackers & pos.pieces(~side)))
            break;                                         // king can't recapture into check

        fromSet = sel & (0ULL - sel);                      // one (lowest) attacker
        aPiece  = pt;
        side    = ~side;
    }

    while (--d > 0)
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}

// ---- Shared state: injected (by reference) into every Worker -----------------
struct SharedState {
    TranspositionTable&                tt;
    const SearchLimits&                limits;
    std::atomic<bool>&                 stop;        // external abort + helper halt
    const std::vector<std::uint64_t>&  gameHistory; // repetition seed (read-only)
};

// ---- The Worker: all per-thread state + the search ---------------------------
struct Worker {
    SharedState&  shared;
    Position      pos;                 // this thread's OWN copy of the root
    int           threadId;
    std::uint64_t nodes = 0;
    bool          stop  = false;       // sticky local copy of the abort decision
    std::chrono::steady_clock::time_point start;

    Move rootBest = MOVE_NONE;
    int  rootDepth = 1;          // depth of the current iterative-deepening iteration

    Move killers[MAX_PLY][2] = {};
    int  history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};
    // Counter-move heuristic: the quiet move that last refuted the opponent's
    // previous move (indexed by our side + that move's from/to).
    Move counterMove[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    // Zobrist keys of all positions on the current line (game history + search
    // path). Invariant: back() == pos.key() at every node. Used for repetitions.
    std::vector<std::uint64_t> repList;

    Worker(SharedState& s, const Position& root, int id)
        : shared(s), pos(root), threadId(id) {
        if (shared.gameHistory.empty()) repList = { pos.key() };
        else                            repList = shared.gameHistory;
        repList.reserve(repList.size() + MAX_PLY + 4);   // no reallocation mid-search
    }

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
        if (shared.stop.load(std::memory_order_relaxed)) { stop = true; return true; }
        if (shared.limits.max_nodes && nodes >= shared.limits.max_nodes) { stop = true; return true; }
        if (shared.limits.movetime_ms > 0 && (nodes & 2047) == 0) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start).count();
            if (ms >= shared.limits.movetime_ms) { stop = true; return true; }
        }
        return false;
    }

    static bool is_capture(const Position& p, Move m) {
        return m.type_of() == EN_PASSANT || p.piece_on(m.to_sq()) != NO_PIECE;
    }

    int score_move(Move m, Move ttMove, Move prevMove, int ply, Color us) const {
        if (m == ttMove) return 2'000'000;
        Piece victim = (m.type_of() == EN_PASSANT) ? make_piece(~us, PAWN)
                                                   : pos.piece_on(m.to_sq());
        if (victim != NO_PIECE) {  // capture -> MVV-LVA (value victim, cheap attacker first)
            int attacker = type_of(pos.piece_on(m.from_sq()));
            int mvvlva   = PIECE_VAL[type_of(victim)] * 16 - attacker;
            // Cheap path: a capture by an equal-or-cheaper piece can't lose
            // material, so skip the SEE call. Otherwise verify; captures that lose
            // material by SEE sort behind every quiet move.
            if (PIECE_VAL[attacker] <= PIECE_VAL[type_of(victim)])
                return 1'000'000 + mvvlva;
            return (see(pos, m) >= 0 ? 1'000'000 : -1'000'000) + mvvlva;
        }
        if (m.type_of() == PROMOTION) return 900'000 + PIECE_VAL[m.promotion_type()];
        if (m == killers[ply][0])     return 800'000;
        if (m == killers[ply][1])     return 700'000;
        if (prevMove != MOVE_NONE &&
            m == counterMove[us][prevMove.from_sq()][prevMove.to_sq()]) return 600'000;
        return history[us][m.from_sq()][m.to_sq()];
    }

    // Sort the list in place, best move first (insertion sort; lists are small).
    void order_moves(MoveList& list, Move ttMove, Move prevMove, int ply, Color us) {
        const int n = list.size();
        int scores[MoveList::CAPACITY];
        for (int i = 0; i < n; ++i) scores[i] = score_move(list[i], ttMove, prevMove, ply, us);
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
        generate_legal_captures(pos, moves);   // captures, en passant, promotions only
        order_moves(moves, MOVE_NONE, MOVE_NONE, ply, pos.side_to_move());

        for (Move m : moves) {

            // Delta pruning: if winning this piece (plus a margin) still can't
            // reach alpha, the capture is hopeless - skip it.
            PieceType victim = (m.type_of() == EN_PASSANT) ? PAWN
                                                           : type_of(pos.piece_on(m.to_sq()));
            int gain = PIECE_VAL[victim] + (m.type_of() == PROMOTION ? 800 : 0);
            if (standPat + gain + 100 < alpha) continue;

            // Skip captures that lose material by static exchange evaluation.
            if (is_capture(pos, m) && see(pos, m) < 0) continue;

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

    int negamax(int depth, int alpha, int beta, int ply, Move prevMove) {
        if (out_of_time()) return 0;
        ++nodes;

        if (ply > 0 && is_draw()) return 0;          // repetition / 50-move = draw

        const bool root    = (ply == 0);
        const bool inCheck = pos.in_check();
        // Check extension, but cap how far a line may run past the nominal depth.
        // Unbounded check extensions let forcing check sequences (common in winning
        // positions) blow up the tree - the likely cause of a normally-20s move
        // taking minutes. Stop extending once we're already deep past the root.
        if (inCheck && ply < 2 * rootDepth) ++depth;

        if (depth <= 0) return quiesce(alpha, beta, ply);

        // Transposition table probe.
        bool ttHit;
        TTEntry* tte = shared.tt.probe(pos.key(), ttHit);
        Move ttMove = MOVE_NONE;
        if (ttHit) {
            ttMove = tte->move;
            if (!root && tte->depth >= depth) {
                int s = from_tt(tte->score, ply);
                if (tte->bound == BOUND_EXACT) return s;
                if (tte->bound == BOUND_LOWER && s >= beta)  return s;
                if (tte->bound == BOUND_UPPER && s <= alpha) return s;
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
            int score = -negamax(depth - 1 - R, -beta, -beta + 1, ply + 1, MOVE_NONE);
            repList.pop_back();
            pos.unmake_null_move(u);
            if (stop) return 0;
            if (score >= beta) return beta;
        }

        MoveList moves;
        generate_legal(pos, moves);
        if (moves.size() == 0)                       // checkmate or stalemate
            return inCheck ? -MATE + ply : 0;

        order_moves(moves, ttMove, prevMove, ply, us);

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
                score = -negamax(depth - 1, -beta, -alpha, ply + 1, m);   // PV: full window
            } else {
                int R = 0;  // late move reduction for quiet, non-checking moves
                if (depth >= 3 && moveCount > 3 && quiet && !givesCheck && !inCheck) {
                    R = LMR.r[std::min(depth, 63)][std::min(moveCount, 63)];
                    if (pvNode) --R;
                    R = std::max(0, std::min(R, depth - 2));
                }

                score = -negamax(depth - 1 - R, -alpha - 1, -alpha, ply + 1, m);  // reduced null window
                if (score > alpha && R > 0)
                    score = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1, m);  // re-search full depth
                if (score > alpha && score < beta)
                    score = -negamax(depth - 1, -beta, -alpha, ply + 1, m);       // re-search full window
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
                    int& h = history[us][m.from_sq()][m.to_sq()];
                    h += depth * depth;
                    if (h > 90'000) h = 90'000;      // cap below killers/counter scores
                    if (prevMove != MOVE_NONE)       // this move refuted prevMove
                        counterMove[us][prevMove.from_sq()][prevMove.to_sq()] = m;
                }
                break;
            }
        }

        // Store in the transposition table. Depth-preferred replacement: keep a
        // deeper entry for the same position, but always take over a slot holding
        // a different position, and always record an exact score.
        Bound flag = (bestScore <= origAlpha) ? BOUND_UPPER
                   : (bestScore >= beta)       ? BOUND_LOWER
                                               : BOUND_EXACT;
        if (tte->key != pos.key() || depth >= tte->depth || flag == BOUND_EXACT)
            *tte = TTEntry{ pos.key(), bestMove,
                            static_cast<std::int16_t>(to_tt(bestScore, ply)),
                            static_cast<std::int8_t>(depth), static_cast<std::uint8_t>(flag) };
        return bestScore;
    }

    // Iterative deepening for this worker. The main worker (threadId 0) drives
    // the time/depth budget; helper workers loop to the same max depth and exit
    // when the shared stop flag is raised by the main worker (or the user).
    SearchResult go() {
        start = std::chrono::steady_clock::now();
        SearchResult result;
        int prevScore = 0;

        for (int d = 1; d <= shared.limits.depth && d < MAX_PLY; ++d) {
            rootBest  = MOVE_NONE;
            rootDepth = d;

            int alpha = -INF, beta = INF, window = 25;
            if (d >= 4) { alpha = prevScore - window; beta = prevScore + window; }

            int score;
            int fails = 0;
            while (true) {                            // aspiration window with widening
                score = negamax(d, alpha, beta, 0, MOVE_NONE);
                if (stop) break;
                // Widen only the bound that failed (the other stays tight). After a
                // couple of failures, or once near mate, open that side fully -
                // doubling forever on an unstable score re-searches the whole tree
                // many times (the cause of pathological slowdowns).
                if (score <= alpha) {
                    window *= 2;
                    alpha = (++fails >= 2 || std::abs(score) >= MATE_IN_MAX)
                                ? -INF : std::max(-INF, score - window);
                } else if (score >= beta) {
                    window *= 2;
                    beta = (++fails >= 2 || std::abs(score) >= MATE_IN_MAX)
                                ? INF : std::min(INF, score + window);
                } else {
                    break;
                }
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
    // NOTE: does NOT clear g_stop (the caller clear_stop()s on the controlling
    // thread) and does NOT clear the TT - entries are validated by key, so they
    // are reused across moves within a game (clear only on ucinewgame).
    SharedState shared{ g_tt, limits, g_stop, history };

    const int nThreads = std::max(1, limits.threads);
    if (nThreads == 1) {
        Worker w(shared, pos, 0);
        return w.go();
    }

    // Lazy SMP: N workers search the same root, sharing only the TT. Each helper
    // gets its own Position copy + history tables; their natural divergence (via
    // TT contention and timing) widens the tree. Helpers run on background
    // threads; the main worker runs here and owns the reported result.
    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(nThreads);
    for (int i = 0; i < nThreads; ++i)
        workers.push_back(std::make_unique<Worker>(shared, pos, i));

    std::vector<std::thread> helpers;
    helpers.reserve(nThreads - 1);
    for (int i = 1; i < nThreads; ++i)
        helpers.emplace_back([&w = *workers[i]] { w.go(); });

    SearchResult result = workers[0]->go();   // main worker drives the budget

    g_stop.store(true, std::memory_order_relaxed);   // halt helpers...
    for (auto& t : helpers) t.join();
    g_stop.store(false, std::memory_order_relaxed);  // ...then disarm (we stopped them, not the user)

    return result;
}

void tt_clear() { g_tt.clear(); }

void tt_resize(int mb) { g_tt.resize(static_cast<std::size_t>(std::max(1, mb))); }

void stop_search()  { g_stop.store(true,  std::memory_order_relaxed); }
void clear_stop()   { g_stop.store(false, std::memory_order_relaxed); }

} // namespace chess
