#pragma once
#ifndef ATTACKS_H
#define ATTACKS_H

#include "types.h"
#include "magics.h"

namespace Attacks {
    extern U64 knightAttacks[64];
    extern U64 kingAttacks[64];
    extern U64 pawnAttacks[2][64];
    void init();

    inline U64 bishopAttacks(Square sq, U64 occupancy) {
        return Magics::getBishopAttacks(sq, occupancy);
    }

    inline U64 rookAttacks(Square sq, U64 occupancy) {
        return Magics::getRookAttacks(sq, occupancy);
    }

    inline U64 queenAttacks(Square sq, U64 occupancy) {
        return Magics::getQueenAttacks(sq, occupancy);
    }
}

#endif // ATTACKS_H
