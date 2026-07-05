#pragma once
#ifndef NNUE_H
#define NNUE_H

#include "types.h"
#include <cstdint>
#include <cstring>
#include <string>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

// ===========================================================================
// NNUE — Efficiently Updatable Neural Network evaluation
// ---------------------------------------------------------------------------
// Architecture: (768 -> HIDDEN_SIZE)x2 -> 1, SCReLU, perspective network.
//
//   Input features (per perspective): 2 colors x 6 piece types x 64 squares.
//   For a given perspective P, a piece of color C, type T on square S maps to
//       relColor = (C == P) ? 0 : 1
//       relSq    = (P == WHITE) ? S : S ^ 56        (vertical mirror)
//       index    = relColor*384 + T*64 + relSq
//   Piece type order is the engine's Piece enum: P,R,N,B,Q,K.
//
//   Both perspective accumulators are maintained incrementally inside
//   Position via addPieceEval/removePieceEval — the single funnel through
//   which every make/undo path (incl. castling, EP, promotion) moves pieces.
//   Integer adds are exactly invertible, so undoMove restores the
//   accumulator bit-for-bit without a stack.
//
//   Quantization: feature-transformer weights/biases scaled by QA=255,
//   output weights by QB=64. Activation is SCReLU: clamp(acc,0,QA)^2.
//   eval_cp = raw * SCALE / (QA*QA*QB), SCALE=400.
//   Range proof: |ftW| <= 512 (trainer clips w to ±1.98), <=32 active
//   features -> |acc| <= 255 + 32*512 = 16639 < 32767. SCReLU SIMD uses
//   madd(v, v*w): |v*w| <= 255*127 = 32385 fits int16; the int32 total
//   is bounded by 2*HIDDEN*255^2*127 ~= 2.11e9 < INT32_MAX.
// ===========================================================================

struct Position;

namespace NNUE {

constexpr int INPUT_SIZE  = 768;
constexpr int HIDDEN_SIZE = 256;
constexpr int QA    = 255;
constexpr int QB    = 64;
constexpr int SCALE = 400;

constexpr uint32_t FILE_MAGIC   = 0x45554E4Eu; // "NNUE" little-endian
constexpr uint32_t FILE_VERSION = 2;           // v2 = SCReLU activation

struct alignas(64) Network {
    int16_t ftWeights[INPUT_SIZE * HIDDEN_SIZE]; // [feature][hidden]
    int16_t ftBias[HIDDEN_SIZE];
    int16_t outWeights[2 * HIDDEN_SIZE];         // [stm half | ntm half]
    int32_t outBias;
};

extern Network* g_net;
extern bool g_loaded;
extern bool g_enabled;

inline bool active() { return g_loaded && g_enabled; }

// Load weights from file. Returns false (and keeps HCE fallback) on any
// error: missing file, bad magic/version, wrong dimensions, short read.
bool loadNetwork(const std::string& path);

// Called once at startup: tries `path`, then next to the executable.
void init(const std::string& path, const char* argv0);

// Perspective-relative feature index.
inline int featureIndex(Color persp, Color c, Piece p, Square sq) {
    int relColor = (c == persp) ? 0 : 1;
    int relSq    = (persp == WHITE) ? sq : (sq ^ 56);
    return relColor * 384 + p * 64 + relSq;
}

// ---------------------------------------------------------------------------
// Accumulator: both perspectives, updated together on every piece add/remove.
// ---------------------------------------------------------------------------
struct alignas(32) Accumulator {
    int16_t v[2][HIDDEN_SIZE]; // [WHITE persp][...], [BLACK persp][...]
};

inline void vecAdd(int16_t* acc, const int16_t* w) {
#if defined(__AVX2__)
    for (int i = 0; i < HIDDEN_SIZE; i += 16) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(acc + i));
        __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(acc + i), _mm256_add_epi16(a, b));
    }
#else
    for (int i = 0; i < HIDDEN_SIZE; i++) acc[i] += w[i];
#endif
}

inline void vecSub(int16_t* acc, const int16_t* w) {
#if defined(__AVX2__)
    for (int i = 0; i < HIDDEN_SIZE; i += 16) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(acc + i));
        __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(acc + i), _mm256_sub_epi16(a, b));
    }
#else
    for (int i = 0; i < HIDDEN_SIZE; i++) acc[i] -= w[i];
#endif
}

inline void addPiece(Accumulator& acc, Color c, Piece p, Square sq) {
    vecAdd(acc.v[WHITE], g_net->ftWeights + featureIndex(WHITE, c, p, sq) * HIDDEN_SIZE);
    vecAdd(acc.v[BLACK], g_net->ftWeights + featureIndex(BLACK, c, p, sq) * HIDDEN_SIZE);
}

inline void removePiece(Accumulator& acc, Color c, Piece p, Square sq) {
    vecSub(acc.v[WHITE], g_net->ftWeights + featureIndex(WHITE, c, p, sq) * HIDDEN_SIZE);
    vecSub(acc.v[BLACK], g_net->ftWeights + featureIndex(BLACK, c, p, sq) * HIDDEN_SIZE);
}

// Reset accumulator to the feature-transformer biases (empty board).
inline void resetAccumulator(Accumulator& acc) {
    if (g_loaded) {
        std::memcpy(acc.v[WHITE], g_net->ftBias, sizeof(int16_t) * HIDDEN_SIZE);
        std::memcpy(acc.v[BLACK], g_net->ftBias, sizeof(int16_t) * HIDDEN_SIZE);
    } else {
        std::memset(acc.v, 0, sizeof(acc.v));
    }
}

// Rebuild the accumulator from scratch off the bitboards.
void refresh(Position& pos);

// Forward pass from the (up-to-date) accumulator; side-to-move perspective.
int evaluate(const Position& pos);

// Debug aid: recompute from scratch and compare with the incremental
// accumulator. Returns true if they match (always true when inactive).
bool verifyAccumulator(const Position& pos);

} // namespace NNUE

#endif // NNUE_H
