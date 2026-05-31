#pragma once
// =============================================================================
// chess/book.hpp - opening book.
//
// Maps a position (by our Zobrist key) to one or more book moves with weights;
// the engine plays a weighted-random book move instantly while in theory, then
// falls back to search once out of book. The built-in book is generated from a
// curated set of main lines (see build_default), so it works with no data file
// and gives opening variety (different games each time).
//
// (We key by our own Zobrist - validated by perft - rather than the Polyglot
// scheme, which would require embedding Polyglot's exact 781-constant table.)
// =============================================================================

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "chess/position.hpp"
#include "chess/move.hpp"

namespace chess {

class OpeningBook {
public:
    void build_default();                    // build the built-in main-line book
    Move probe(const Position& pos) const;   // a weighted-random book move, or MOVE_NONE
    bool empty() const { return book_.empty(); }

private:
    struct Entry { Move move; int weight; };
    std::unordered_map<std::uint64_t, std::vector<Entry>> book_;
};

} // namespace chess
