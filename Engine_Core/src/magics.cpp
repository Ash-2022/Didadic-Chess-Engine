#include "magics.h"
#include <iostream>
#include <cstring>

namespace Magics {

    Magic rookMagics[64];
    Magic bishopMagics[64];

    U64 rookAttackTable[102400];
    U64 bishopAttackTable[5248];

    static void extractBits(U64 mask, int* bits, int* count) {
        *count = 0;
        U64 temp = mask;
        while (temp) {
            bits[(*count)++] = __builtin_ctzll(temp);
            temp &= temp - 1;
        }
    }

    static void generateBlockers(U64 mask, U64* blockers, int* numBlockers) {
        int bits[12];
        int bitCount;
        extractBits(mask, bits, &bitCount);

        int total = 1 << bitCount;
        for (int i = 0; i < total; i++) {
            U64 blocker = 0;
            for (int j = 0; j < bitCount; j++) {
                if (i & (1 << j))
                    blocker |= 1ULL << bits[j];
            }
            blockers[i] = blocker;
        }
        *numBlockers = total;
    }

    class MagicFinder {
    private:
        std::mt19937_64 rng;
        U64 randomU64Sparse() { return rng() & rng() & rng(); }
    public:
        explicit MagicFinder(uint64_t seed) : rng(seed) {}

        U64 findMagic(int square, bool isBishop, U64 mask, int bits) {
            U64 blockers[4096], attacks[4096], used[4096];
            int numBlockers;

            generateBlockers(mask, blockers, &numBlockers);

            for (int i = 0; i < numBlockers; i++) {
                attacks[i] = isBishop
                    ? bishopAttacksSlow(square, blockers[i])
                    : rookAttacksSlow(square, blockers[i]);
            }

            for (int attempt = 0; attempt < 100000000; attempt++) {
                U64 magic = randomU64Sparse();
                if (popcount((mask * magic) & 0xFF00000000000000ULL) < 6) continue;

                std::memset(used, 0, sizeof(used));
                bool fail = false;
                for (int i = 0; i < numBlockers; i++) {
                    U64 index = (blockers[i] * magic) >> (64 - bits);
                    if (used[index] == 0)
                        used[index] = attacks[i];
                    else if (used[index] != attacks[i]) {
                        fail = true;
                        break;
                    }
                }
                if (!fail) return magic;
            }

            std::cerr << "ERROR: Failed to find magic for square " << square << std::endl;
            return 0;
        }
    };

    void init() {
        std::cout << "Initializing Magic Bitboards..." << std::endl;

        MagicFinder finder(MAGIC_SEED);

        // Rooks
        U64* rookTablePtr = rookAttackTable;
        for (int sq = 0; sq < 64; sq++) {
            U64 mask = rookMaskSlow(sq);
            int bits = popcount(mask);
            int indexCount = 1 << bits;

            U64 magic = finder.findMagic(sq, false, mask, bits);

            rookMagics[sq].mask = mask;
            rookMagics[sq].magic = magic;
            rookMagics[sq].shift = 64 - bits;
            rookMagics[sq].attacks = rookTablePtr;

            U64 blockers[4096];
            int numBlockers;
            generateBlockers(mask, blockers, &numBlockers);

            for (int i = 0; i < numBlockers; i++) {
                U64 index = (blockers[i] * magic) >> (64 - bits);
                rookTablePtr[index] = rookAttacksSlow(sq, blockers[i]);
            }
            rookTablePtr += indexCount;
        }

        // Bishops
        U64* bishopTablePtr = bishopAttackTable;
        for (int sq = 0; sq < 64; sq++) {
            U64 mask = bishopMaskSlow(sq);
            int bits = popcount(mask);
            int indexCount = 1 << bits;

            U64 magic = finder.findMagic(sq, true, mask, bits);

            bishopMagics[sq].mask = mask;
            bishopMagics[sq].magic = magic;
            bishopMagics[sq].shift = 64 - bits;
            bishopMagics[sq].attacks = bishopTablePtr;

            U64 blockers[512];
            int numBlockers;
            generateBlockers(mask, blockers, &numBlockers);

            for (int i = 0; i < numBlockers; i++) {
                U64 index = (blockers[i] * magic) >> (64 - bits);
                bishopTablePtr[index] = bishopAttacksSlow(sq, blockers[i]);
            }
            bishopTablePtr += indexCount;
        }

        std::cout << "Magic Bitboards initialized." << std::endl;
    }

} // namespace Magics
