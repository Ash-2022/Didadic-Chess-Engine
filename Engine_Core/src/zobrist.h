#pragma once
#include "types.h"
#include <cstdint>

namespace Zobrist {
    extern U64 piece[2][6][64];
    extern U64 castling[16];
    extern U64 enPassant[64];
    extern U64 sideToMove;

    void init();
}
