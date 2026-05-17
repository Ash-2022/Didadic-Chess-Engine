#include "attacks.h"

namespace Attacks {
    U64 knightAttacks[64];
    U64 kingAttacks[64];
    U64 pawnAttacks[2][64];

    void init() {
        for (int sq = 0; sq < 64; sq++) {
            int r = sq / 8, f = sq % 8;

            // Knight
            const int kOff[8][2] = {
                {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}
            };
            for (auto& o : kOff) {
                int nr = r + o[0], nf = f + o[1];
                if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
                    knightAttacks[sq] |= 1ULL << (nr * 8 + nf);
            }

            // King
            for (int dr = -1; dr <= 1; dr++) {
                for (int df = -1; df <= 1; df++) {
                    if (!dr && !df) continue;
                    int nr = r + dr, nf = f + df;
                    if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
                        kingAttacks[sq] |= 1ULL << (nr * 8 + nf);
                }
            }

            // Pawn attacks
            if (r < 7) {
                if (f > 0) pawnAttacks[WHITE][sq] |= 1ULL << (sq + 7);
                if (f < 7) pawnAttacks[WHITE][sq] |= 1ULL << (sq + 9);
            }
            if (r > 0) {
                if (f > 0) pawnAttacks[BLACK][sq] |= 1ULL << (sq - 9);
                if (f < 7) pawnAttacks[BLACK][sq] |= 1ULL << (sq - 7);
            }
        }
    }
}
