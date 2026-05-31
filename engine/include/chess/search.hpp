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

// Ask the running search to abort as soon as possible (thread-safe). The search
// returns its best result so far. Used to implement UCI `stop` / start-over.
void stop_search();

// Clear the abort flag. MUST be called (on the controlling thread) before
// starting a search that should run to completion - do NOT clear it from inside
// the search thread, or a `stop` that arrives just after launch could be lost.
void clear_stop();

} // namespace chess
