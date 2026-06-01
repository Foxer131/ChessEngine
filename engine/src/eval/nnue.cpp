// =============================================================================
// NNUE inference - Phase 1 (see docs/NNUE.md).
//
// Scalar, quantized forward pass for the "NNUE-lite" net:
//   768 sparse features -> feature transformer (768 x L1) -> accumulator[2][L1]
//   concatenated (stm first) -> 2*L1 -> 32 -> 32 -> 1, clipped-ReLU between layers.
//
// This file owns ONLY shared, read-only weights (the loaded network) and pure
// functions over an Accumulator. The Accumulator itself is per-position state
// living on Position (Phase 2), so nothing here needs concurrency reasoning -
// the weights are shared like the TT, exactly as the architecture intends.
//
// File format (our own, little-endian; do NOT parse Stockfish's .nnue):
//   char   magic[4] = "NN01"
//   int32  l1                       (must equal nnue::L1)
//   int16  ft_weights[INPUT_DIM*L1] (column-major: [feature][neuron])
//   int16  ft_bias[L1]
//   int8   h1_weights[2*L1*H1]      ([out][in])
//   int32  h1_bias[H1]
//   int8   h2_weights[H1*H2]
//   int32  h2_bias[H2]
//   int8   out_weights[H2]
//   int32  out_bias
// =============================================================================

#include "chess/nnue.hpp"
#include "chess/position.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <vector>

namespace chess {
namespace nnue {
namespace {

constexpr int H1 = 32;
constexpr int H2 = 32;

// Quantization scales (must match the trainer). Activations are clipped to
// [0, QA]; weights are scaled so int8*int16 sums stay in int32.
constexpr int QA       = 127;   // clipped-ReLU ceiling (activation range)
constexpr int FT_SHIFT = 6;     // accumulator -> activation right-shift
constexpr int OUT_SCALE = 16;   // final dequantization to centipawns

// The loaded network (shared, read-only after load()).
struct Network {
    std::vector<std::int16_t> ftW;   // [INPUT_DIM * L1]
    std::vector<std::int16_t> ftB;   // [L1]
    std::vector<std::int8_t>  h1W;   // [2L1 * H1]
    std::vector<std::int32_t> h1B;   // [H1]
    std::vector<std::int8_t>  h2W;   // [H1 * H2]
    std::vector<std::int32_t> h2B;   // [H2]
    std::vector<std::int8_t>  outW;  // [H2]
    std::int32_t              outB = 0;
};

Network g_net;
bool    g_loaded = false;

std::int32_t crelu(std::int32_t x) { return std::clamp<std::int32_t>(x, 0, QA); }

template <typename T>
bool read_vec(std::ifstream& f, std::vector<T>& v, std::size_t n) {
    v.resize(n);
    f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n * sizeof(T)));
    return bool(f);
}

} // namespace

bool is_loaded() { return g_loaded; }

void unload() { g_loaded = false; }

bool load(const std::string& path) {
    g_loaded = false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4];
    f.read(magic, 4);
    if (!f || std::memcmp(magic, "NN01", 4) != 0) return false;

    std::int32_t l1 = 0;
    f.read(reinterpret_cast<char*>(&l1), 4);
    if (!f || l1 != L1) return false;   // shape must match this build

    Network n;
    if (!read_vec(f, n.ftW, std::size_t(INPUT_DIM) * L1)) return false;
    if (!read_vec(f, n.ftB, L1))                          return false;
    if (!read_vec(f, n.h1W, std::size_t(2 * L1) * H1))    return false;
    if (!read_vec(f, n.h1B, H1))                          return false;
    if (!read_vec(f, n.h2W, std::size_t(H1) * H2))        return false;
    if (!read_vec(f, n.h2B, H2))                          return false;
    if (!read_vec(f, n.outW, H2))                         return false;
    f.read(reinterpret_cast<char*>(&n.outB), 4);
    if (!f) return false;

    g_net = std::move(n);
    g_loaded = true;
    return true;
}

// Recompute the accumulator from scratch: bias + the column of ftW for every
// active feature, for both perspectives. The Phase-2 incremental updates must
// always reproduce exactly this.
void refresh(Accumulator& acc, const Position& pos) {
    for (Color p = WHITE; p <= BLACK; p = Color(p + 1))
        for (int i = 0; i < L1; ++i)
            acc.v[p][i] = g_net.ftB[i];

    for (Color c = WHITE; c <= BLACK; c = Color(c + 1))
        for (PieceType pt = PAWN; pt <= KING; pt = PieceType(pt + 1)) {
            Bitboard b = pos.pieces(c, pt);
            while (b) {
                Square s = pop_lsb(b);
                for (Color p = WHITE; p <= BLACK; p = Color(p + 1)) {
                    const std::int16_t* col = &g_net.ftW[std::size_t(feature_index(p, c, pt, s)) * L1];
                    for (int i = 0; i < L1; ++i)
                        acc.v[p][i] = std::int16_t(acc.v[p][i] + col[i]);
                }
            }
        }
    acc.valid = true;
}

// Forward pass from a valid accumulator. The side-to-move's perspective goes in
// the first L1 inputs, the opponent's in the second - so the result is naturally
// side-to-move-relative (same convention as the HCE evaluate()).
int forward(const Accumulator& acc, Color stm) {
    std::int32_t in[2 * L1];
    const Color opp = ~stm;
    for (int i = 0; i < L1; ++i) in[i]      = crelu(acc.v[stm][i] >> FT_SHIFT);
    for (int i = 0; i < L1; ++i) in[L1 + i] = crelu(acc.v[opp][i] >> FT_SHIFT);

    std::int32_t a1[H1];
    for (int j = 0; j < H1; ++j) {
        std::int32_t s = g_net.h1B[j];
        const std::int8_t* w = &g_net.h1W[std::size_t(j) * (2 * L1)];
        for (int i = 0; i < 2 * L1; ++i) s += std::int32_t(w[i]) * in[i];
        a1[j] = crelu(s >> FT_SHIFT);
    }

    std::int32_t a2[H2];
    for (int j = 0; j < H2; ++j) {
        std::int32_t s = g_net.h2B[j];
        const std::int8_t* w = &g_net.h2W[std::size_t(j) * H1];
        for (int i = 0; i < H1; ++i) s += std::int32_t(w[i]) * a1[i];
        a2[j] = crelu(s >> FT_SHIFT);
    }

    std::int32_t out = g_net.outB;
    for (int i = 0; i < H2; ++i) out += std::int32_t(g_net.outW[i]) * a2[i];
    return out / OUT_SCALE;   // dequantize to centipawns
}

// ---- Incremental updates ----------------------------------------------------
void add_piece(Accumulator& acc, Color c, PieceType pt, Square sq) {
    for (Color p = WHITE; p <= BLACK; p = Color(p + 1)) {
        const std::int16_t* col = &g_net.ftW[std::size_t(feature_index(p, c, pt, sq)) * L1];
        for (int i = 0; i < L1; ++i) acc.v[p][i] = std::int16_t(acc.v[p][i] + col[i]);
    }
}

void remove_piece(Accumulator& acc, Color c, PieceType pt, Square sq) {
    for (Color p = WHITE; p <= BLACK; p = Color(p + 1)) {
        const std::int16_t* col = &g_net.ftW[std::size_t(feature_index(p, c, pt, sq)) * L1];
        for (int i = 0; i < L1; ++i) acc.v[p][i] = std::int16_t(acc.v[p][i] - col[i]);
    }
}

bool accumulator_matches_refresh(const Accumulator& acc, const Position& pos) {
    Accumulator ref;
    refresh(ref, pos);
    for (Color p = WHITE; p <= BLACK; p = Color(p + 1))
        for (int i = 0; i < L1; ++i)
            if (acc.v[p][i] != ref.v[p][i]) return false;
    return true;
}

void make_random_net(unsigned seed) {
    std::mt19937 rng(seed);
    auto i16 = [&](int lo, int hi) {
        return std::int16_t(std::uniform_int_distribution<int>(lo, hi)(rng));
    };
    auto i8  = [&](int lo, int hi) {
        return std::int8_t(std::uniform_int_distribution<int>(lo, hi)(rng));
    };
    Network n;
    n.ftW.resize(std::size_t(INPUT_DIM) * L1); for (auto& w : n.ftW) w = i16(-8, 8);
    n.ftB.resize(L1);                          for (auto& w : n.ftB) w = i16(-16, 16);
    n.h1W.resize(std::size_t(2 * L1) * H1);    for (auto& w : n.h1W) w = i8(-4, 4);
    n.h1B.resize(H1);                          for (auto& w : n.h1B) w = std::uniform_int_distribution<int>(-64, 64)(rng);
    n.h2W.resize(std::size_t(H1) * H2);        for (auto& w : n.h2W) w = i8(-4, 4);
    n.h2B.resize(H2);                          for (auto& w : n.h2B) w = std::uniform_int_distribution<int>(-64, 64)(rng);
    n.outW.resize(H2);                         for (auto& w : n.outW) w = i8(-8, 8);
    n.outB = std::uniform_int_distribution<int>(-64, 64)(rng);
    g_net = std::move(n);
    g_loaded = true;
}

} // namespace nnue
} // namespace chess
