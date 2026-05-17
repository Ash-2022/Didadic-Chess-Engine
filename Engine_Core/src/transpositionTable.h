#pragma once
#include "types.h"

struct TTEntry {
    U64 key;
    int depth;
    int score;
    TTFlag flag;
    Move bestMove;
};

constexpr int TT_SIZE = 1 << 25; // 16M entries (~512MB)

extern TTEntry ttTable[TT_SIZE];

inline TTEntry& ttEntry(U64 key) {
    return ttTable[key & (TT_SIZE - 1)];
}

void initTranspositionTable();
