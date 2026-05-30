#pragma once
// =============================================================================
// chess/search.hpp - the search: find the best move for a position.
//
// A modern alpha-beta searcher (negamax + PVS) with iterative deepening,
// a transposition table, quiescence search, move ordering, null-move pruning,
// late move reductions and aspiration windows. See search.cpp.
// =============================================================================

#include <cstdint>
#include "chess/position.hpp"
#include "chess/move.hpp"

namespace chess {

struct SearchLimits {
    int           depth      = 64;  // max iterative-deepening depth
    int           movetime_ms = 0;  // wall-clock budget in ms (0 = no time limit)
    std::uint64_t max_nodes   = 0;  // node budget (0 = no node limit)
};

struct SearchResult {
    Move          best  = MOVE_NONE;  // best move found
    int           score = 0;          // centipawns, side-to-move perspective
    int           depth = 0;          // last fully completed depth
    std::uint64_t nodes = 0;          // nodes visited
};

// Search `pos` under `limits` and return the best move. `pos` is left unchanged.
SearchResult search(Position& pos, const SearchLimits& limits);

} // namespace chess
