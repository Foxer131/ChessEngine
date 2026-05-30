#include "chess/attacks.hpp"
#include "chess/bitboard.hpp"

#include <cstddef>
#include <cstdint>

// =============================================================================
// Magic bitboards for the sliding pieces (bishop / rook; queen = both).
//
// Idea: a slider's attacks depend only on the occupied squares ALONG its rays.
// For each square we
//   1. build a "relevant occupancy" mask = the rays minus the board edges,
//   2. for every subset of that mask, precompute the true attack set with a
//      slow ray-walker,
//   3. find a 64-bit "magic" so that  (occupancy * magic) >> shift  maps each
//      subset to a unique slot in a per-square table.
// A lookup is then just: index the table with that hashed occupancy. The magics
// are searched for once, at first use; nothing is hard-coded.
// =============================================================================

namespace chess {
namespace {

constexpr bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

const int ROOK_DIRS[4][2]   = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
const int BISHOP_DIRS[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// Slow but obviously-correct: walk each direction until the edge or a blocker
// (the blocker square is included in the attack set).
Bitboard sliding_attacks(Square s, Bitboard occ, const int (*dirs)[2]) {
    Bitboard attacks = 0;
    const int f0 = file_of(s), r0 = rank_of(s);
    for (int d = 0; d < 4; ++d) {
        int f = f0 + dirs[d][0], r = r0 + dirs[d][1];
        while (on_board(f, r)) {
            Square sq = make_square(File(f), Rank(r));
            attacks |= square_bb(sq);
            if (occ & square_bb(sq)) break;
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return attacks;
}

// Relevant-occupancy mask: the rays minus the edge squares (a blocker sitting on
// the edge can't change what lies beyond it) and minus the square itself.
Bitboard slider_mask(Square s, const int (*dirs)[2]) {
    Bitboard mask = 0;
    const int f0 = file_of(s), r0 = rank_of(s);
    for (int d = 0; d < 4; ++d) {
        int f = f0 + dirs[d][0], r = r0 + dirs[d][1];
        // stop one short of the edge: the NEXT step must still be on-board
        while (on_board(f, r) && on_board(f + dirs[d][0], r + dirs[d][1])) {
            mask |= square_bb(make_square(File(f), Rank(r)));
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return mask;
}

// Small, fast xorshift PRNG. A magic with few set bits is more likely to work,
// so we AND three draws together to bias toward sparse candidates.
struct PRNG {
    std::uint64_t s;
    explicit PRNG(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }
    std::uint64_t sparse() { return next() & next() & next(); }
};

struct Magic {
    Bitboard        mask    = 0;
    Bitboard        magic   = 0;
    const Bitboard* attacks = nullptr;  // points into the shared pool below
    int             shift   = 0;        // 64 - relevant bit count

    std::size_t index(Bitboard occ) const {
        return static_cast<std::size_t>(((occ & mask) * magic) >> shift);
    }
};

// Known total table sizes for variable-shift magics.
constexpr int ROOK_TABLE_SIZE   = 102400;
constexpr int BISHOP_TABLE_SIZE = 5248;

struct SliderTables {
    Bitboard rookPool[ROOK_TABLE_SIZE]{};
    Bitboard bishopPool[BISHOP_TABLE_SIZE]{};
    Magic    rook[SQUARE_NB];
    Magic    bishop[SQUARE_NB];

    SliderTables() {
        init(rook,   rookPool,   ROOK_DIRS);
        init(bishop, bishopPool, BISHOP_DIRS);
    }

    static void init(Magic magics[], Bitboard* pool, const int (*dirs)[2]) {
        PRNG rng(0x246CCB2D3B4015ECULL);
        Bitboard occ[4096], ref[4096];   // one entry per occupancy subset
        Bitboard* ptr = pool;

        for (Square s = SQ_A1; s <= SQ_H8; s = Square(s + 1)) {
            Magic& m   = magics[s];
            m.mask     = slider_mask(s, dirs);
            const int bits = popcount(m.mask);
            m.shift    = 64 - bits;
            m.attacks  = ptr;
            const int size = 1 << bits;

            // Enumerate every subset of the mask (carry-rippler) and its attacks.
            int n = 0;
            Bitboard b = 0;
            do {
                occ[n] = b;
                ref[n] = sliding_attacks(s, b, dirs);
                ++n;
                b = (b - m.mask) & m.mask;
            } while (b);

            // Search for a collision-free magic for this square.
            for (;;) {
                Bitboard candidate = rng.sparse();
                if (popcount((m.mask * candidate) >> 56) < 6) continue; // quick reject

                for (int i = 0; i < size; ++i) ptr[i] = 0;
                bool ok = true;
                for (int i = 0; i < n; ++i) {
                    std::size_t idx = static_cast<std::size_t>((occ[i] * candidate) >> m.shift);
                    if (ptr[idx] == 0) ptr[idx] = ref[i];
                    else if (ptr[idx] != ref[i]) { ok = false; break; }
                }
                if (ok) { m.magic = candidate; break; }
            }
            ptr += size;
        }
    }
};

const SliderTables& tables() {
    static const SliderTables t;  // built once, on first use
    return t;
}

} // namespace

Bitboard rook_attacks(Square s, Bitboard occ) {
    const Magic& m = tables().rook[s];
    return m.attacks[m.index(occ)];
}

Bitboard bishop_attacks(Square s, Bitboard occ) {
    const Magic& m = tables().bishop[s];
    return m.attacks[m.index(occ)];
}

Bitboard queen_attacks(Square s, Bitboard occ) {
    return rook_attacks(s, occ) | bishop_attacks(s, occ);
}

} // namespace chess
