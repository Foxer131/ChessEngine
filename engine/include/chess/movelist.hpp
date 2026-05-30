#pragma once
// =============================================================================
// chess/movelist.hpp - a fixed-capacity, stack-allocated buffer of moves.
//
// A chess position has at most ~218 legal moves, so a 256-slot array never
// overflows and costs zero heap allocation - exactly what the search's hot path
// needs. Generators append with add(); consumers iterate with range-for.
// =============================================================================

#include "chess/move.hpp"

namespace chess {

class MoveList {
public:
    static constexpr int CAPACITY = 256;

    void add(Move m) { moves_[size_++] = m; }

    int  size()  const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear()       { size_ = 0; }

    Move  operator[](int i) const { return moves_[i]; }
    Move& operator[](int i)       { return moves_[i]; }  // mutable: move ordering reorders in place

    // Enable range-for: `for (Move m : list) ...`
    const Move* begin() const { return moves_; }
    const Move* end()   const { return moves_ + size_; }
    Move* begin() { return moves_; }
    Move* end()   { return moves_ + size_; }

private:
    Move moves_[CAPACITY];
    int  size_ = 0;
};

} // namespace chess
