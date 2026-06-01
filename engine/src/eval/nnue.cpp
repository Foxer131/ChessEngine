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
#include <iterator>
#include <random>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#define NNUE_AVX2 1
#endif

namespace chess {
namespace nnue {

// The network compiled into the binary (tools/embed_net.py -> embedded_net.cpp).
// Weak fallbacks here so the engine still links if no net is embedded yet; the
// real definitions in embedded_net.cpp override them.
extern const unsigned char EMBEDDED_NET[];
extern const std::size_t   EMBEDDED_NET_SIZE;

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

// Parse bullet's raw .bin layout (four i16 arrays, no header) from a byte buffer.
// Both file and embedded loaders funnel through here. `size` must hold at least
// the four arrays; trailing padding (bullet aligns to 64 bytes) is ignored.
bool parse_net(const unsigned char* p, std::size_t size, Network& n) {
    const std::size_t need = (std::size_t(INPUT_DIM) * L1 + L1 + 2 * L1 + 1) * sizeof(std::int16_t);
    if (size < need) return false;
    auto take = [&](std::vector<std::int16_t>& v, std::size_t count) {
        v.resize(count);
        std::memcpy(v.data(), p, count * sizeof(std::int16_t));
        p += count * sizeof(std::int16_t);
    };
    take(n.ftW,  std::size_t(INPUT_DIM) * L1);
    take(n.ftB,  L1);
    take(n.outW, std::size_t(2) * L1);
    std::memcpy(&n.outB, p, sizeof(std::int16_t));
    return true;
}

} // namespace

bool is_loaded() { return g_loaded; }

void unload() { g_loaded = false; }

bool load(const std::string& path) {
    g_loaded = false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    Network n;
    if (!parse_net(buf.data(), buf.size(), n)) return false;
    g_net = std::move(n);
    g_loaded = true;
    return true;
}

bool load_embedded() {
    g_loaded = false;
    if (EMBEDDED_NET_SIZE == 0) return false;
    Network n;
    if (!parse_net(EMBEDDED_NET, EMBEDDED_NET_SIZE, n)) return false;
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
namespace {
// Sum over L1 of screlu(acc[i]) * w[i]. The AVX2 path uses the Lizard SCReLU
// trick: instead of (v*v)*w (which needs wide intermediates), reorder as
// v * (v*w). With v=clamp(acc,0,QA) in [0,255] and output weights clipped so
// |w| <= QA (=255*... actually <= 127 here), the product v*w fits in int16
// (255*127 < 32767). Then _mm256_madd_epi16(v, v*w) computes the pairwise
// products v*(v*w) = v^2*w = screlu*w AND sums adjacent pairs into int32 in one
// instruction - no unpack/widen. Per-lane int32 sums stay small (16 terms each);
// we widen to int64 only at the final horizontal reduction.
inline std::int64_t dot_screlu(const std::int16_t* a, const std::int16_t* w) {
#if NNUE_AVX2
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa   = _mm256_set1_epi16(std::int16_t(QA));
    __m256i sum = _mm256_setzero_si256();   // 8 int32 lanes
    for (int i = 0; i < L1; i += 16) {
        __m256i v  = _mm256_load_si256(reinterpret_cast<const __m256i*>(a + i));
        v = _mm256_min_epi16(_mm256_max_epi16(v, zero), qa);        // clamp [0,QA]
        __m256i wv = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
        __m256i vw = _mm256_mullo_epi16(v, wv);                     // v*w (fits int16)
        sum = _mm256_add_epi32(sum, _mm256_madd_epi16(v, vw));      // += sum_pairs(v * vw)
    }
    // Horizontal reduce 8 int32 lanes -> int64 (widen to be safe against the
    // theoretical full-saturation total).
    std::int32_t lanes[8];
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(lanes), sum);
    std::int64_t out = 0;
    for (int k = 0; k < 8; ++k) out += lanes[k];
    return out;
#else
    std::int64_t out = 0;
    for (int i = 0; i < L1; ++i) out += std::int64_t(screlu(a[i])) * w[i];
    return out;
#endif
}
} // namespace

int forward(const Accumulator& acc, Color stm) {
    const Color opp = ~stm;
    std::int64_t out = dot_screlu(acc.v[stm], &g_net.outW[0])
                     + dot_screlu(acc.v[opp], &g_net.outW[L1]);

    out /= QA;                       // SCReLU output is QA*QA*QB; reduce to QA*QB
    out += g_net.outB;               // bias is at QA*QB
    out *= SCALE;
    out /= (std::int64_t(QA) * QB);  // dequantize to centipawns
    return int(out);
}

// ---- Incremental updates ----------------------------------------------------
// One perspective's accumulator += (Add ? +col : -col), over L1 int16. The AVX2
// path does 16 int16 per instruction; the scalar path is the reference (and the
// fallback when AVX2 is unavailable). Both must produce identical results - the
// accumulator_matches_refresh gate verifies it.
namespace {
template <bool Add>
inline void acc_update(std::int16_t* dst, const std::int16_t* col) {
#if NNUE_AVX2
    static_assert(L1 % 16 == 0, "L1 must be a multiple of 16 for the AVX2 path");
    for (int i = 0; i < L1; i += 16) {
        __m256i d = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(col + i));
        d = Add ? _mm256_add_epi16(d, w) : _mm256_sub_epi16(d, w);
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), d);
    }
#else
    for (int i = 0; i < L1; ++i)
        dst[i] = std::int16_t(Add ? dst[i] + col[i] : dst[i] - col[i]);
#endif
}
} // namespace

void add_piece(Accumulator& acc, Color c, PieceType pt, Square sq) {
    for (Color p = WHITE; p <= BLACK; p = Color(p + 1))
        acc_update<true>(acc.v[p], &g_net.ftW[std::size_t(feature_index(p, c, pt, sq)) * L1]);
}

void remove_piece(Accumulator& acc, Color c, PieceType pt, Square sq) {
    for (Color p = WHITE; p <= BLACK; p = Color(p + 1))
        acc_update<false>(acc.v[p], &g_net.ftW[std::size_t(feature_index(p, c, pt, sq)) * L1]);
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
