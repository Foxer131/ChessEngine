// =============================================================================
// NNUE inference (see docs/NNUE.md). Architecture and quantization match the
// `bullet` trainer's `simple` example exactly, so we load bullet's raw .bin
// output directly - no custom exporter, no format mismatch (the #1 NNUE bug).
//
//   (768 -> L1) x2  ->  1        [perspective accumulators concatenated: stm, ntm]
//   activation: SCReLU (squared clipped ReLU), QA=255 QB=64, eval scale 400
//
// File layout (bullet `Network`, little-endian, all i16):
//   feature_weights[768 * L1]   column-major: feature f's column = [f*L1 .. f*L1+L1)
//   feature_bias[L1]
//   output_weights[2 * L1]      first L1 = stm side, next L1 = ntm side
//   output_bias                 (1)
//
// This file owns ONLY shared, read-only weights + pure functions over an
// Accumulator. The Accumulator is per-position state on Position, so nothing here
// needs concurrency reasoning - weights are shared like the TT.
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

// Quantization / scaling - MUST match the trainer (bullet simple.rs defaults).
constexpr std::int32_t QA    = 255;   // feature-transformer quantization
constexpr std::int32_t QB    = 64;    // output-weights quantization
constexpr std::int32_t SCALE = 400;   // eval scale (centipawns)

struct Network {
    std::vector<std::int16_t> ftW;   // [INPUT_DIM * L1] feature weights (col-major)
    std::vector<std::int16_t> ftB;   // [L1] feature bias
    std::vector<std::int16_t> outW;  // [2*L1] output weights (stm half, ntm half)
    std::int16_t              outB = 0;
};

Network g_net;
bool    g_loaded = false;

// Squared clipped ReLU: clamp to [0, QA] then square (takes i16 acc -> i32).
inline std::int32_t screlu(std::int16_t x) {
    std::int32_t y = std::clamp<std::int32_t>(x, 0, QA);
    return y * y;
}

template <typename T>
bool read_vec(std::ifstream& f, std::vector<T>& v, std::size_t n) {
    v.resize(n);
    f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n * sizeof(T)));
    return bool(f);
}

} // namespace

bool is_loaded() { return g_loaded; }

void unload() { g_loaded = false; }

// Load bullet's raw .bin (sized exactly to the four arrays; no header). We infer
// nothing - the layout is fixed by our build's L1.
bool load(const std::string& path) {
    g_loaded = false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    Network n;
    if (!read_vec(f, n.ftW,  std::size_t(INPUT_DIM) * L1)) return false;
    if (!read_vec(f, n.ftB,  L1))                          return false;
    if (!read_vec(f, n.outW, std::size_t(2) * L1))         return false;
    f.read(reinterpret_cast<char*>(&n.outB), sizeof(std::int16_t));
    if (!f) return false;

    // Reject if the file is larger than expected (wrong L1 / wrong file).
    f.peek();
    if (!f.eof()) return false;

    g_net = std::move(n);
    g_loaded = true;
    return true;
}

// Recompute the accumulator from scratch: bias + the column of ftW for every
// active feature, for both perspectives. The incremental updates must always
// reproduce exactly this (the Phase-2 correctness gate).
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

// Forward pass from a valid accumulator. The side-to-move's perspective uses the
// first L1 output weights, the opponent's the second half - so the result is
// side-to-move-relative (same convention as the HCE evaluate()).
int forward(const Accumulator& acc, Color stm) {
    const Color opp = ~stm;
    std::int64_t out = 0;
    for (int i = 0; i < L1; ++i) out += std::int64_t(screlu(acc.v[stm][i])) * g_net.outW[i];
    for (int i = 0; i < L1; ++i) out += std::int64_t(screlu(acc.v[opp][i])) * g_net.outW[L1 + i];

    out /= QA;                       // SCReLU output is QA*QA*QB; reduce to QA*QB
    out += g_net.outB;               // bias is at QA*QB
    out *= SCALE;
    out /= (std::int64_t(QA) * QB);  // dequantize to centipawns
    return int(out);
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

// Test/bootstrap helper: a small deterministic in-memory net (so the
// incremental==refresh gate runs without a trained file). Not for play.
void make_random_net(unsigned seed) {
    std::mt19937 rng(seed);
    auto i16 = [&](int lo, int hi) {
        return std::int16_t(std::uniform_int_distribution<int>(lo, hi)(rng));
    };
    Network n;
    n.ftW.resize(std::size_t(INPUT_DIM) * L1); for (auto& w : n.ftW)  w = i16(-32, 32);
    n.ftB.resize(L1);                          for (auto& w : n.ftB)  w = i16(-32, 32);
    n.outW.resize(std::size_t(2) * L1);        for (auto& w : n.outW) w = i16(-32, 32);
    n.outB = i16(-32, 32);
    g_net = std::move(n);
    g_loaded = true;
}

} // namespace nnue
} // namespace chess
