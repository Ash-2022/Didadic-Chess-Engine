#include "transpositionTable.h"
#include <cstring>

// ✅ actual storage
TTEntry ttTable[TT_SIZE];

void initTranspositionTable() {
    std::memset(ttTable, 0, sizeof(ttTable));
}
