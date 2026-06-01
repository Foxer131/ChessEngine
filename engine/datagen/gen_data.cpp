// =============================================================================
// gen_data - self-play training-data generator for NNUE (see docs/NNUE.md and
// tools/training/README.md).
//
// Plays the engine against itself at a fixed node budget. From a light random
// opening (for diversity), it records every QUIET position along the game as
//     <fen> | <cp> | <result>
// where cp is the search score from White's perspective and result is the game's
// outcome (1.0 / 0.5 / 0.0 for White). This "score + game result" pairing is the
// standard NNUE training target; `bullet` consumes exactly this.
//
//   gen_data <out.txt> [games] [nodes] [seed]
//
// Single-threaded and deterministic given the seed, so runs are reproducible and
// shardable (run N copies with different seeds + output files, then concatenate).
// =============================================================================

#include "chess/position.hpp"
#include "chess/movegen.hpp"
#include "chess/movelist.hpp"
#include "chess/search.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace chess;

namespace {

constexpr int   MAX_PLIES     = 240;   // adjudicate as a draw if we reach this
constexpr int   RANDOM_PLIES  = 8;     // random opening moves for diversity
constexpr int   ADJ_WIN_CP    = 1500;  // |score| this high for a few plies => decided
constexpr int   ADJ_WIN_COUNT = 4;

bool is_capture(const Position& p, Move m) {
    return m.type_of() == EN_PASSANT || p.piece_on(m.to_sq()) != NO_PIECE;
}

// One self-play game. Appends "<fen>|<cp>" lines for quiet positions to `lines`
// (cp is White-relative); returns the game result for White (1.0/0.5/0.0).
double play_game(std::mt19937_64& rng, std::uint64_t nodes,
                 std::vector<std::pair<std::string,int>>& lines) {
    Position pos;
    pos.set_startpos();
    std::vector<std::uint64_t> hist = { pos.key() };

    SearchLimits lim;
    lim.depth     = 64;
    lim.max_nodes = nodes;
    lim.threads   = 1;

    const std::size_t startIdx = lines.size();
    double result = 0.5;
    int decidedCount = 0, decidedSign = 0;

    for (int ply = 0; ply < MAX_PLIES; ++ply) {
        MoveList moves;
        generate_legal(pos, moves);
        if (moves.size() == 0) {                 // mate or stalemate
            result = pos.in_check() ? (pos.side_to_move() == WHITE ? 0.0 : 1.0) : 0.5;
            break;
        }
        if (pos.halfmove_clock() >= 100) { result = 0.5; break; }

        Move chosen;
        if (ply < RANDOM_PLIES) {                // random opening for diversity
            chosen = moves[std::uniform_int_distribution<int>(0, moves.size() - 1)(rng)];
        } else {
            clear_stop();
            SearchResult r = search(pos, lim, hist);
            chosen = r.best;
            if (chosen == MOVE_NONE) chosen = moves[0];

            int whiteCp = (pos.side_to_move() == WHITE) ? r.score : -r.score;

            // Record only quiet positions: not in check, best move not a capture,
            // and a real centipawn score (skip mate scores - they aren't an eval
            // and would saturate the training sigmoid).
            if (!pos.in_check() && !is_capture(pos, chosen) && std::abs(whiteCp) < 29000)
                lines.emplace_back(pos.to_fen(), whiteCp);

            // Early adjudication when the score is lopsided for a few plies.
            int sign = (whiteCp > ADJ_WIN_CP) ? 1 : (whiteCp < -ADJ_WIN_CP ? -1 : 0);
            if (sign != 0 && sign == decidedSign) {
                if (++decidedCount >= ADJ_WIN_COUNT) { result = sign > 0 ? 1.0 : 0.0; break; }
            } else { decidedSign = sign; decidedCount = (sign != 0) ? 1 : 0; }
        }

        Position::Undo u;
        pos.make_move(chosen, u);
        hist.push_back(pos.key());
    }

    // Stamp the game result onto every position recorded this game (filled by
    // the caller writing fen|cp|result).
    (void)startIdx;
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: gen_data <out.txt> [games=1000] [nodes=5000] [seed=1]\n";
        return 1;
    }
    const std::string out = argv[1];
    const int           games = (argc > 2) ? std::atoi(argv[2]) : 1000;
    const std::uint64_t nodes = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 5000;
    const std::uint64_t seed  = (argc > 4) ? std::strtoull(argv[4], nullptr, 10) : 1;

    std::ofstream f(out);
    if (!f) { std::cerr << "cannot open " << out << "\n"; return 1; }

    std::mt19937_64 rng(seed);
    tt_clear();

    std::uint64_t totalPositions = 0;
    for (int g = 0; g < games; ++g) {
        std::vector<std::pair<std::string,int>> lines;
        double result = play_game(rng, nodes, lines);
        // Result as "1.0"/"0.5"/"0.0" (White-relative) - the form bullet's text
        // loader expects.
        const char* res = (result == 1.0) ? "1.0" : (result == 0.0) ? "0.0" : "0.5";
        for (auto& [fen, cp] : lines)
            f << fen << " | " << cp << " | " << res << "\n";
        totalPositions += lines.size();
        tt_clear();   // independent games: don't leak TT knowledge across them

        if ((g + 1) % 50 == 0 || g + 1 == games) {
            std::cerr << "\rgames " << (g + 1) << "/" << games
                      << "  positions " << totalPositions << std::flush;
        }
    }
    std::cerr << "\ndone: " << totalPositions << " positions -> " << out << "\n";
    return 0;
}
