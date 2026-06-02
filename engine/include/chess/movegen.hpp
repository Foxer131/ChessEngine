#pragma once
// =============================================================================
// chess/movegen.hpp - legal move generation + perft.
//
// generate_legal fills a MoveList with every legal move for the side to move.
// perft enumerates the move tree to a fixed depth (the correctness gate for the
// generator: its node counts must match published reference values).
//
// Both take Position by reference because they make/unmake moves internally;
// the position is always restored to its original state on return.
// =============================================================================

#include <cstdint>
#include "chess/position.hpp"
#include "chess/movelist.hpp"

namespace chess {

void generate_legal(Position& pos, MoveList& list);

// Legal "noisy" moves only: captures, en passant and promotions (the quiescence
// search's move set). Produces the same moves generate_legal would, restricted to
// that subset and in the same relative order.
void generate_legal_captures(Position& pos, MoveList& list);

std::uint64_t perft(Position& pos, int depth);

} // namespace chess
