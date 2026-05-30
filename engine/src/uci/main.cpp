// =============================================================================
// UCI front-end: a thin loop translating the UCI protocol to/from the engine.
// The GUI (or Cutechess/Arena) launches this executable and speaks UCI over
// stdin/stdout. The engine itself is stateless per command - `position`
// rebuilds the board from scratch, so we never drift out of sync.
// =============================================================================

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "chess/position.hpp"
#include "chess/movegen.hpp"
#include "chess/movelist.hpp"
#include "chess/search.hpp"

using namespace chess;

namespace {

std::string square_to_uci(Square s) {
    std::string r;
    r += char('a' + file_of(s));
    r += char('1' + rank_of(s));
    return r;
}

// Long-algebraic UCI string for a move, e.g. "e2e4", "e1g1" (castling),
// "e7e8q" (promotion).
std::string move_to_uci(Move m) {
    if (m == MOVE_NONE) return "0000";
    std::string s = square_to_uci(m.from_sq()) + square_to_uci(m.to_sq());
    if (m.type_of() == PROMOTION)
        s += "  nbrq"[m.promotion_type()];  // PieceType KNIGHT..QUEEN -> n b r q
    return s;
}

// Match a UCI token to one of the legal moves (handles castling / ep / promotion
// because the legal move already carries the right type).
Move find_move(Position& pos, const std::string& uci) {
    MoveList list;
    generate_legal(pos, list);
    for (Move m : list)
        if (move_to_uci(m) == uci) return m;
    return MOVE_NONE;
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

    while (is >> token) {
        if (token == "moves") continue;
        Move m = find_move(pos, token);
        if (m == MOVE_NONE) break;  // illegal/garbage token
        Position::Undo u;
        pos.make_move(m, u);
    }
}

// go [depth N] [movetime MS] [nodes N] [wtime MS] [btime MS] [infinite]
void cmd_go(Position& pos, std::istringstream& is) {
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
        // winc/binc/movestogo/ponder and their values are ignored
    }

    SearchLimits lim;  // defaults: depth 64, no time/node cap
    if (depth > 0)    lim.depth = depth;
    if (movetime > 0) lim.movetime_ms = movetime;
    if (nodes > 0)    lim.max_nodes = static_cast<std::uint64_t>(nodes);
    if (infinite)     lim.depth = 12;  // we search synchronously; cap to stay responsive

    if (depth == 0 && movetime == 0 && nodes == 0 && !infinite) {
        int myTime = (pos.side_to_move() == WHITE) ? wtime : btime;
        if (myTime > 0) lim.movetime_ms = std::max(10, myTime / 30);  // simple budget
        else            lim.depth = 8;                                // sane default
    }

    SearchResult r = search(pos, lim);

    std::cout << "info depth " << r.depth << " score cp " << r.score
              << " nodes " << r.nodes << " pv " << move_to_uci(r.best) << "\n";
    std::cout << "bestmove " << move_to_uci(r.best) << std::endl;
}

} // namespace

int main() {
    std::ios::sync_with_stdio(false);

    Position pos;
    pos.set_startpos();

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string cmd;
        is >> cmd;

        if (cmd == "uci") {
            std::cout << "id name ChessEngine 0.1\n";
            std::cout << "id author Joao\n";
            std::cout << "uciok\n";
        } else if (cmd == "isready") {
            std::cout << "readyok\n";
        } else if (cmd == "ucinewgame") {
            pos.set_startpos();
        } else if (cmd == "position") {
            cmd_position(pos, is);
        } else if (cmd == "go") {
            cmd_go(pos, is);
        } else if (cmd == "quit") {
            break;
        }
        std::cout.flush();
    }
    return 0;
}
