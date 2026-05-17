#include "zobrist.h"
#include <random>

namespace Zobrist {
    U64 piece[2][6][64];
    U64 castling[16];
    U64 enPassant[64];
    U64 sideToMove;

    static U64 rand64() {
        static std::mt19937_64 rng(20240517);
        return rng();
    }

    void init() {
        for (int c = 0; c < 2; c++)
            for (int p = 0; p < 6; p++)
                for (int sq = 0; sq < 64; sq++)
                    piece[c][p][sq] = rand64();

        for (int i = 0; i < 16; i++)
            castling[i] = rand64();

        for (int sq = 0; sq < 64; sq++)
            enPassant[sq] = rand64();

        sideToMove = rand64();
    }
}
