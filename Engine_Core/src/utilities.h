#pragma once
#include "types.h"
#include <vector>

inline Square popLSB(U64& bb) {
    Square sq = static_cast<Square>(__builtin_ctzll(bb));
    bb &= bb - 1;
    return sq;
}

inline bool isSet(U64 bb, Square sq) {
    return bb & (1ULL << sq);
}

inline void addMove(std::vector<Move>& moves, Square from, Square to,
                    MoveFlags flags, Piece promo = NO_PIECE) {
    moves.push_back(Move{from, to, flags, promo});
}
