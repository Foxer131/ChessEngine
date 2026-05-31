#include "chess/book.hpp"
#include "chess/movegen.hpp"
#include "chess/movelist.hpp"

#include <random>
#include <sstream>
#include <string>

namespace chess {
namespace {

std::string sq_uci(Square s) {
    std::string r;
    r += char('a' + file_of(s));
    r += char('1' + rank_of(s));
    return r;
}

std::string mv_uci(Move m) {
    std::string s = sq_uci(m.from_sq()) + sq_uci(m.to_sq());
    if (m.type_of() == PROMOTION) s += "  nbrq"[m.promotion_type()];
    return s;
}

// Curated main lines (UCI). Replaying each records, for every position along the
// way, the move that continues the line - for BOTH colors, so the engine plays
// book whether it is White or Black. Shared early positions (e.g. after 1.e4)
// accumulate several replies, giving variety via weighted-random selection.
const char* const LINES[] = {
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7",   // Ruy Lopez
    "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3",        // Italian
    "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6",             // Scotch
    "e2e4 e7e5 b1c3 g8f6 g1f3 b8c6",                       // Vienna / Four Knights
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6",   // Sicilian Najdorf
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5",   // Sicilian Sveshnikov-ish
    "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6",                       // French
    "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 b8d7",             // Caro-Kann
    "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7",             // Queen's Gambit Declined
    "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 e7e6",             // Slav
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6",             // King's Indian
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3",                  // Nimzo-Indian
    "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6",                       // Queen's Indian
    "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6",                       // English
    "g1f3 d7d5 d2d4 g8f6 c2c4 e7e6",                       // Reti -> QGD
};

} // namespace

void OpeningBook::build_default() {
    book_.clear();
    for (const char* line : LINES) {
        Position p;
        p.set_startpos();
        std::istringstream is(line);
        std::string tok;
        while (is >> tok) {
            MoveList list;
            generate_legal(p, list);
            Move found = MOVE_NONE;
            for (Move m : list)
                if (mv_uci(m) == tok) { found = m; break; }
            if (found == MOVE_NONE) break;  // malformed line - stop replaying it

            auto& vec = book_[p.key()];
            bool merged = false;
            for (auto& e : vec)
                if (e.move == found) { ++e.weight; merged = true; break; }
            if (!merged) vec.push_back({found, 1});

            Position::Undo u;
            p.make_move(found, u);
        }
    }
}

Move OpeningBook::probe(const Position& pos) const {
    auto it = book_.find(pos.key());
    if (it == book_.end() || it->second.empty()) return MOVE_NONE;

    const auto& vec = it->second;
    int total = 0;
    for (const auto& e : vec) total += e.weight;

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, total - 1);
    int r = dist(rng);
    for (const auto& e : vec) {
        if (r < e.weight) return e.move;
        r -= e.weight;
    }
    return vec.front().move;
}

} // namespace chess
