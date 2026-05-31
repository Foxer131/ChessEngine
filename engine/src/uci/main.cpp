// =============================================================================
// UCI front-end. The search runs on a WORKER THREAD so the main thread keeps
// reading stdin and can abort it (`stop`, or starting a new game mid-search)
// via stop_search(). This is what makes "New Game while the engine is thinking"
// responsive instead of freezing until the old search finishes.
// =============================================================================

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "chess/position.hpp"
#include "chess/movegen.hpp"
#include "chess/movelist.hpp"
#include "chess/search.hpp"
#include "chess/book.hpp"

using namespace chess;

namespace {

std::thread g_worker;
std::mutex  g_cout;   // serializes stdout between the worker and the main thread
OpeningBook g_book;
bool        g_own_book = true;
std::vector<std::uint64_t> g_history;   // zobrist keys of the game, for repetition

std::string square_to_uci(Square s) {
    std::string r;
    r += char('a' + file_of(s));
    r += char('1' + rank_of(s));
    return r;
}

std::string move_to_uci(Move m) {
    if (m == MOVE_NONE) return "0000";
    std::string s = square_to_uci(m.from_sq()) + square_to_uci(m.to_sq());
    if (m.type_of() == PROMOTION)
        s += "  nbrq"[m.promotion_type()];
    return s;
}

Move find_move(Position& pos, const std::string& uci) {
    MoveList list;
    generate_legal(pos, list);
    for (Move m : list)
        if (move_to_uci(m) == uci) return m;
    return MOVE_NONE;
}

// Abort and join any in-progress search (no-op if idle).
void stop_and_join() {
    if (g_worker.joinable()) {
        stop_search();
        g_worker.join();
    }
}

// position [startpos | fen <6 fields>] [moves <m1> <m2> ...]
void cmd_position(Position& pos, std::istringstream& is) {
    std::string token;
    if (!(is >> token)) return;

    if (token == "startpos") {
        pos.set_startpos();
    } else if (token == "fen") {
        std::string fen, part;
        for (int i = 0; i < 6 && (is >> part); ++i)
            fen += (i ? " " : "") + part;
        pos.set_fen(fen);
    } else {
        return;
    }

    g_history.clear();
    g_history.push_back(pos.key());
    while (is >> token) {
        if (token == "moves") continue;
        Move m = find_move(pos, token);
        if (m == MOVE_NONE) break;
        Position::Undo u;
        pos.make_move(m, u);
        g_history.push_back(pos.key());
    }
}

// The worker body: search, then print info + bestmove (under the cout lock).
void run_search(Position& pos, SearchLimits lim, std::vector<std::uint64_t> hist) {
    SearchResult r = search(pos, lim, hist);
    std::lock_guard<std::mutex> lk(g_cout);
    std::cout << "info depth " << r.depth << " score cp " << r.score
              << " nodes " << r.nodes << " pv " << move_to_uci(r.best) << "\n";
    std::cout << "bestmove " << move_to_uci(r.best) << std::endl;
}

// go [depth N] [movetime MS] [nodes N] [wtime MS] [btime MS] [infinite]
void cmd_go(Position& pos, std::istringstream& is) {
    stop_and_join();   // never decide/print over a running search

    // In book? Play the book move instantly and skip the search.
    if (g_own_book) {
        Move bm = g_book.probe(pos);
        if (bm != MOVE_NONE) {
            std::lock_guard<std::mutex> lk(g_cout);
            std::cout << "info string book move\n";
            std::cout << "bestmove " << move_to_uci(bm) << std::endl;
            return;
        }
    }

    int depth = 0, movetime = 0, wtime = 0, btime = 0;
    long long nodes = 0;
    bool infinite = false;

    std::string token;
    while (is >> token) {
        if      (token == "depth")    is >> depth;
        else if (token == "movetime") is >> movetime;
        else if (token == "nodes")    is >> nodes;
        else if (token == "wtime")    is >> wtime;
        else if (token == "btime")    is >> btime;
        else if (token == "infinite") infinite = true;
    }

    SearchLimits lim;
    if (depth > 0)    lim.depth = depth;
    if (movetime > 0) lim.movetime_ms = movetime;
    if (nodes > 0)    lim.max_nodes = static_cast<std::uint64_t>(nodes);
    if (infinite)     lim.depth = 64;   // bounded by `stop` now, so this is safe

    if (depth == 0 && movetime == 0 && nodes == 0 && !infinite) {
        int myTime = (pos.side_to_move() == WHITE) ? wtime : btime;
        if (myTime > 0) lim.movetime_ms = std::max(10, myTime / 30);
        else            lim.depth = 8;
    }

    stop_and_join();                      // ensure no prior search is running
    clear_stop();                         // arm a fresh search BEFORE launching the worker
    g_worker = std::thread(run_search, std::ref(pos), lim, g_history);
}

} // namespace

int main() {
    std::ios::sync_with_stdio(false);

    Position pos;
    pos.set_startpos();
    g_book.build_default();

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string cmd;
        is >> cmd;

        if (cmd == "uci") {
            std::lock_guard<std::mutex> lk(g_cout);
            std::cout << "id name ChessEngine 0.1\n";
            std::cout << "id author Joao\n";
            std::cout << "option name OwnBook type check default true\n";
            std::cout << "uciok\n" << std::flush;
        } else if (cmd == "isready") {
            std::lock_guard<std::mutex> lk(g_cout);
            std::cout << "readyok\n" << std::flush;
        } else if (cmd == "setoption") {
            std::string tok, name, value;
            while (is >> tok) {
                if      (tok == "name")  is >> name;
                else if (tok == "value") is >> value;
            }
            if (name == "OwnBook") g_own_book = (value == "true");
        } else if (cmd == "ucinewgame") {
            stop_and_join();
            tt_clear();
            pos.set_startpos();
        } else if (cmd == "position") {
            stop_and_join();            // don't mutate the board under the worker
            cmd_position(pos, is);
        } else if (cmd == "go") {
            cmd_go(pos, is);
        } else if (cmd == "stop") {
            stop_and_join();            // aborts; the worker prints its bestmove
        } else if (cmd == "quit") {
            break;
        }
    }

    stop_and_join();
    return 0;
}
