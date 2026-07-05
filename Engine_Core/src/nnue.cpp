#include "nnue.h"
#include "position.h"
#include <cstdio>
#include <iostream>
#include <memory>

namespace NNUE {

Network* g_net   = nullptr;
bool g_loaded    = false;
bool g_enabled   = true;

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool loadNetwork(const std::string& path) {
#ifdef _WIN32
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif

    uint32_t header[4]; // magic, version, input, hidden
    if (std::fread(header, sizeof(uint32_t), 4, f) != 4 ||
        header[0] != FILE_MAGIC || header[1] != FILE_VERSION ||
        header[2] != (uint32_t)INPUT_SIZE || header[3] != (uint32_t)HIDDEN_SIZE) {
        std::fclose(f);
        std::cerr << "info string NNUE: bad header in " << path << std::endl;
        return false;
    }

    if (!g_net) {
        // aligned to 64 via alignas on the struct; operator new respects it (C++17)
        g_net = new Network();
    }

    bool ok =
        std::fread(g_net->ftWeights,  sizeof(int16_t), INPUT_SIZE * HIDDEN_SIZE, f) == (size_t)(INPUT_SIZE * HIDDEN_SIZE) &&
        std::fread(g_net->ftBias,     sizeof(int16_t), HIDDEN_SIZE, f)             == (size_t)HIDDEN_SIZE &&
        std::fread(g_net->outWeights, sizeof(int16_t), 2 * HIDDEN_SIZE, f)         == (size_t)(2 * HIDDEN_SIZE) &&
        std::fread(&g_net->outBias,   sizeof(int32_t), 1, f)                       == 1;
    std::fclose(f);

    if (!ok) {
        std::cerr << "info string NNUE: short read in " << path << std::endl;
        return false;
    }
    g_loaded = true;
    return true;
}

void init(const std::string& path, const char* argv0) {
    if (loadNetwork(path)) {
        std::cout << "NNUE: loaded " << path << std::endl;
        return;
    }
    // Try next to the executable
    if (argv0) {
        std::string exe(argv0);
        size_t slash = exe.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string sibling = exe.substr(0, slash + 1) + path;
            if (loadNetwork(sibling)) {
                std::cout << "NNUE: loaded " << sibling << std::endl;
                return;
            }
        }
    }
    std::cout << "NNUE: no usable network at " << path
              << ", using hand-crafted eval" << std::endl;
}

// ---------------------------------------------------------------------------
// Full refresh from bitboards
// ---------------------------------------------------------------------------

void refresh(Position& pos) {
    if (!g_loaded) return;
    resetAccumulator(pos.acc);
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            U64 bb = pos.pieces[c][p];
            while (bb) {
                Square sq = Square(__builtin_ctzll(bb));
                bb &= bb - 1;
                addPiece(pos.acc, Color(c), Piece(p), sq);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Forward pass: SCReLU(acc) . outWeights, side-to-move half first.
// SCReLU(x) = clamp(x,0,QA)^2, computed as madd(v, v*w): v*w fits int16
// because |w| <= 127 (trainer clips to ±1.98 * QB).
// ---------------------------------------------------------------------------

static inline int32_t dotHalf(const int16_t* acc, const int16_t* w) {
#if defined(__AVX2__)
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa   = _mm256_set1_epi16(QA);
    __m256i sum = _mm256_setzero_si256();
    for (int i = 0; i < HIDDEN_SIZE; i += 16) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(acc + i));
        a = _mm256_min_epi16(_mm256_max_epi16(a, zero), qa);
        __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
        __m256i aw = _mm256_mullo_epi16(a, b);
        sum = _mm256_add_epi32(sum, _mm256_madd_epi16(a, aw));
    }
    __m128i lo = _mm256_castsi256_si128(sum);
    __m128i hi = _mm256_extracti128_si256(sum, 1);
    __m128i s  = _mm_add_epi32(lo, hi);
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_cvtsi128_si32(s);
#else
    int32_t sum = 0;
    for (int i = 0; i < HIDDEN_SIZE; i++) {
        int32_t a = acc[i];
        if (a < 0) a = 0; else if (a > QA) a = QA;
        sum += a * a * w[i];
    }
    return sum;
#endif
}

int evaluate(const Position& pos) {
    const Color us   = pos.sideToMove;
    const Color them = (us == WHITE) ? BLACK : WHITE;

    int64_t sum = g_net->outBias;
    sum += dotHalf(pos.acc.v[us],   g_net->outWeights);
    sum += dotHalf(pos.acc.v[them], g_net->outWeights + HIDDEN_SIZE);

    int score = (int)(sum * SCALE / ((int64_t)QA * QA * QB));

    // Keep well clear of mate-score territory
    if (score >  15000) score =  15000;
    if (score < -15000) score = -15000;
    return score;
}

// ---------------------------------------------------------------------------
// Debug verification: incremental accumulator vs from-scratch rebuild
// ---------------------------------------------------------------------------

bool verifyAccumulator(const Position& pos) {
    if (!active()) return true;
    Accumulator fresh;
    resetAccumulator(fresh);
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            U64 bb = pos.pieces[c][p];
            while (bb) {
                Square sq = Square(__builtin_ctzll(bb));
                bb &= bb - 1;
                addPiece(fresh, Color(c), Piece(p), sq);
            }
        }
    }
    return std::memcmp(fresh.v, pos.acc.v, sizeof(fresh.v)) == 0;
}

} // namespace NNUE
