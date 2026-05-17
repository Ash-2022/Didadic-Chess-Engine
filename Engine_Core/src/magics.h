#pragma once
#ifndef MAGICS_H
#define MAGICS_H

#include "types.h"
#include <random>

namespace Magics {

    constexpr uint64_t MAGIC_SEED = 20241201ULL;

    struct Magic {
        U64 mask     = 0;
        U64 magic    = 0;
        U64* attacks = nullptr;
        int shift    = 0;
    };

    extern U64 rookAttackTable[102400];
    extern U64 bishopAttackTable[5248];

    extern Magic rookMagics[64];
    extern Magic bishopMagics[64];

    void init();

    inline U64 getRookAttacks(Square sq, U64 occupancy) {
        const Magic& m = rookMagics[sq];
        return m.attacks[((occupancy & m.mask) * m.magic) >> m.shift];
    }

    inline U64 getBishopAttacks(Square sq, U64 occupancy) {
        const Magic& m = bishopMagics[sq];
        return m.attacks[((occupancy & m.mask) * m.magic) >> m.shift];
    }

    inline U64 getQueenAttacks(Square sq, U64 occupancy) {
        return getRookAttacks(sq, occupancy) | getBishopAttacks(sq, occupancy);
    }

    constexpr U64 rookMaskSlow(int square) {
        U64 result = 0;
        int rank = square / 8, file = square % 8;
        for (int r = rank + 1; r <= 6; r++) result |= 1ULL << (r * 8 + file);
        for (int r = rank - 1; r >= 1; r--) result |= 1ULL << (r * 8 + file);
        for (int f = file + 1; f <= 6; f++) result |= 1ULL << (rank * 8 + f);
        for (int f = file - 1; f >= 1; f--) result |= 1ULL << (rank * 8 + f);
        return result;
    }

    constexpr U64 bishopMaskSlow(int square) {
        U64 result = 0;
        int rank = square / 8, file = square % 8;
        for (int r = rank+1, f = file+1; r <= 6 && f <= 6; r++, f++) result |= 1ULL << (r*8+f);
        for (int r = rank+1, f = file-1; r <= 6 && f >= 1; r++, f--) result |= 1ULL << (r*8+f);
        for (int r = rank-1, f = file+1; r >= 1 && f <= 6; r--, f++) result |= 1ULL << (r*8+f);
        for (int r = rank-1, f = file-1; r >= 1 && f >= 1; r--, f--) result |= 1ULL << (r*8+f);
        return result;
    }

    constexpr U64 rookAttacksSlow(int square, U64 blockers) {
        U64 result = 0;
        int rank = square / 8, file = square % 8;
        for (int r = rank+1; r < 8; r++) { result |= 1ULL << (r*8+file); if (blockers & (1ULL << (r*8+file))) break; }
        for (int r = rank-1; r >= 0; r--) { result |= 1ULL << (r*8+file); if (blockers & (1ULL << (r*8+file))) break; }
        for (int f = file+1; f < 8; f++) { result |= 1ULL << (rank*8+f); if (blockers & (1ULL << (rank*8+f))) break; }
        for (int f = file-1; f >= 0; f--) { result |= 1ULL << (rank*8+f); if (blockers & (1ULL << (rank*8+f))) break; }
        return result;
    }

    constexpr U64 bishopAttacksSlow(int square, U64 blockers) {
        U64 result = 0;
        int rank = square / 8, file = square % 8;
        for (int r=rank+1,f=file+1; r<8&&f<8; r++,f++) { result |= 1ULL<<(r*8+f); if (blockers & (1ULL<<(r*8+f))) break; }
        for (int r=rank+1,f=file-1; r<8&&f>=0; r++,f--) { result |= 1ULL<<(r*8+f); if (blockers & (1ULL<<(r*8+f))) break; }
        for (int r=rank-1,f=file+1; r>=0&&f<8; r--,f++) { result |= 1ULL<<(r*8+f); if (blockers & (1ULL<<(r*8+f))) break; }
        for (int r=rank-1,f=file-1; r>=0&&f>=0; r--,f--) { result |= 1ULL<<(r*8+f); if (blockers & (1ULL<<(r*8+f))) break; }
        return result;
    }

    constexpr int popcount(U64 x) {
        int count = 0;
        while (x) { count++; x &= x - 1; }
        return count;
    }

} // namespace Magics

#endif // MAGICS_H
