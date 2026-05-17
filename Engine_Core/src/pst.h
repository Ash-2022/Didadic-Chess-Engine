#pragma once
#include "types.h"

extern const int PawnPSTWhite[64];
extern const int PawnPSTBlack[64];

extern const int KnightPSTWhite[64];
extern const int KnightPSTBlack[64];

// // Pawn PST (opening/middlegame)
// static constexpr int PawnPST[64] = {
//      0,  0,  0,  0,  0,  0,  0,  0,
//      5,  5,  5, -5, -5,  5,  5,  5,  // Rank 2: changed -20 to -5
//      5, -5,-10,  0,  0,-10, -5,  5,
//      0,  0,  0, 10, 10,  0,  0,  0,  // Rank 4: changed 20 to 10
//      5,  5, 10, 15, 15, 10,  5,  5,  // Rank 5: changed 25 to 15
//     10, 10, 20, 30, 30, 20, 10, 10,
//     50, 50, 50, 50, 50, 50, 50, 50,
//      0,  0,  0,  0,  0,  0,  0,  0
// };

// // Knight PST
// static constexpr int KnightPST[64] = {
//    -10, -5,  0,  0,  0,  0, -5,-10,  // Rank 1
//    -10,  0,  5,  5,  5,  5,  0,-10,  // Rank 2
//      0,  5, 10, 15, 15, 10,  5,  0,  // Rank 3
//      0,  5, 15, 20, 20, 15,  5,  0,  // Rank 4
//      0,  5, 15, 20, 20, 15,  5,  0,  // Rank 5
//      0,  5, 10, 15, 15, 10,  5,  0,  // Rank 6
//    -10,  0,  5,  5,  5,  5,  0,-10,  // Rank 7
//    -10, -5,  0,  0,  0,  0, -5,-10   // Rank 8
// };
