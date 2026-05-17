#pragma once
#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"

constexpr U64 squareBB(Square sq) {
    return 1ULL << sq;
}

#endif // BITBOARD_H
