#pragma once
// =============================================================================
// chess/eval.hpp - static evaluation.
//
// evaluate() returns a score in centipawns from the SIDE-TO-MOVE's perspective
// (positive = better for the side to move) - the convention negamax wants.
// This is a simple hand-crafted baseline (material + tapered piece-square
// bonuses); it's the most swappable part of the engine (PeSTO / NNUE later).
// =============================================================================

#include "chess/position.hpp"

namespace chess {

int evaluate(const Position& pos);

} // namespace chess
